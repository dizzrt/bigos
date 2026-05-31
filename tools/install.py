#!/usr/bin/env python3
import os
import sys
from getopt import GetoptError, getopt
from typing import BinaryIO

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BOOT_SOURCE_DIR = os.path.join(PROJECT_ROOT, 'src', 'arch', 'x86', 'boot')
VDISK_PATH = os.path.join(PROJECT_ROOT, 'build', 'os.vhd')
MBR_PATH = ''
DBR_PATH = ''
EXDBR_PATH = ''
BOOT_PATH = ''

BYTES_PER_SECTOR = 512
MBR_PARTITION_TABLE_OFFSET = 0x1BE
MBR_PARTITION_TABLE_SIZE = 64
PARTITION_ENTRY_SIZE = 16
EXFAT_PARTITION_TYPE = 0x07
EXFAT_MAIN_BOOT_SECTORS = 12
EXFAT_EXTENDED_DBR_SECTORS = 8
EXFAT_ENTRY_SIZE = 32
EXFAT_FILE_ENTRY = 0x85
EXFAT_STREAM_ENTRY = 0xC0
EXFAT_NAME_ENTRY = 0xC1
EXFAT_ATTR_DIRECTORY = 0x10
EXFAT_ATTR_ARCHIVE = 0x20
EXFAT_STREAM_NO_FAT_CHAIN = 0x02
BOOT_MAX_LOAD_BYTES = 0x80000


class UnsupportedLayoutError(RuntimeError):
    pass


class ExfatPartition:
    def __init__(self, entry_offset: int, lba: int, sectors: int, active: bool):
        self.entry_offset = entry_offset
        self.lba = lba
        self.sectors = sectors
        self.active = active


class ExfatBootRegion:
    def __init__(
        self,
        partition_lba: int,
        bytes_per_sector: int,
        sectors_per_cluster: int,
        cluster_heap_lba: int,
        root_cluster: int,
    ):
        self.partition_lba = partition_lba
        self.bytes_per_sector = bytes_per_sector
        self.sectors_per_cluster = sectors_per_cluster
        self.cluster_heap_lba = cluster_heap_lba
        self.root_cluster = root_cluster

    def cluster_lba(self, cluster: int) -> int:
        if cluster < 2:
            raise UnsupportedLayoutError('unsupported-layout: invalid exfat cluster number')
        return self.cluster_heap_lba + (cluster - 2) * self.sectors_per_cluster


class ExfatFile:
    def __init__(self, first_cluster: int, data_length: int, contiguous: bool):
        self.first_cluster = first_cluster
        self.data_length = data_length
        self.contiguous = contiguous


def debugSuccess(msg: object):
    print('\033[32m' + str(msg) + '\033[0m')


def debugError(msg: object, abort: bool = True):
    print('\033[31merror:' + str(msg) + '\033[0m')
    exit(1)


def debugWarn(msg: object):
    print('\033[93m' + str(msg) + '\033[0m')


def read_exact(vhd: BinaryIO, size: int) -> bytes:
    data = vhd.read(size)
    if len(data) != size:
        raise UnsupportedLayoutError('unsupported-layout: unexpected end of disk image')
    return data


def read_partition_table(vhd: BinaryIO) -> bytes:
    vhd.seek(MBR_PARTITION_TABLE_OFFSET)
    ptes = read_exact(vhd, MBR_PARTITION_TABLE_SIZE)
    if len(ptes) != MBR_PARTITION_TABLE_SIZE:
        raise UnsupportedLayoutError('unsupported-layout: incomplete mbr partition table')
    return ptes


def find_exfat_partition(vhd: BinaryIO, require_active: bool = False) -> ExfatPartition:
    ptes = read_partition_table(vhd)

    found: ExfatPartition | None = None
    for index in range(4):
        pte_offset = index * PARTITION_ENTRY_SIZE
        pte = ptes[pte_offset : pte_offset + PARTITION_ENTRY_SIZE]
        active = pte[0] == 0x80
        partition_type = pte[4]
        lba = int.from_bytes(pte[8:12], 'little')
        sectors = int.from_bytes(pte[12:16], 'little')

        if partition_type != EXFAT_PARTITION_TYPE:
            continue
        if lba == 0 or sectors == 0:
            raise UnsupportedLayoutError('unsupported-layout: exfat partition has empty bounds')
        if require_active and not active:
            continue
        if found is not None:
            raise UnsupportedLayoutError('unsupported-layout: multiple exfat partitions')
        found = ExfatPartition(MBR_PARTITION_TABLE_OFFSET + pte_offset, lba, sectors, active)

    if found is None:
        raise UnsupportedLayoutError('unsupported-layout: no supported exfat partition found')
    return found


def read_exfat_boot_region(vhd: BinaryIO, partition_lba: int) -> ExfatBootRegion:
    vhd.seek(partition_lba * BYTES_PER_SECTOR)
    sector = read_exact(vhd, BYTES_PER_SECTOR)
    if sector[3:11] != b'EXFAT   ':
        raise UnsupportedLayoutError('unsupported-layout: partition is not exfat')

    bytes_per_sector = 1 << sector[0x6C]
    sectors_per_cluster = 1 << sector[0x6D]
    if bytes_per_sector != BYTES_PER_SECTOR:
        raise UnsupportedLayoutError('unsupported-layout: exfat sector size does not match installer sector size')
    if sectors_per_cluster == 0:
        raise UnsupportedLayoutError('unsupported-layout: invalid exfat sectors per cluster')

    partition_offset = int.from_bytes(sector[0x40:0x48], 'little')
    cluster_heap_offset = int.from_bytes(sector[0x58:0x5C], 'little')
    root_cluster = int.from_bytes(sector[0x60:0x64], 'little')
    if partition_offset not in (0, partition_lba):
        raise UnsupportedLayoutError('unsupported-layout: unexpected exfat partition offset')

    return ExfatBootRegion(
        partition_lba=partition_lba,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        cluster_heap_lba=partition_lba + cluster_heap_offset,
        root_cluster=root_cluster,
    )


def parse_name(entry: bytes) -> str:
    chars = []
    for offset in range(2, EXFAT_ENTRY_SIZE, 2):
        codepoint = int.from_bytes(entry[offset : offset + 2], 'little')
        if codepoint == 0:
            break
        chars.append(chr(codepoint))
    return ''.join(chars)


def read_directory(vhd: BinaryIO, info: ExfatBootRegion, first_cluster: int) -> bytes:
    lba = info.cluster_lba(first_cluster)
    size = info.sectors_per_cluster * info.bytes_per_sector
    vhd.seek(lba * info.bytes_per_sector)
    return read_exact(vhd, size)


def find_directory_child(directory: bytes, name: str, want_directory: bool) -> ExfatFile:
    offset = 0
    while offset + EXFAT_ENTRY_SIZE <= len(directory):
        entry = directory[offset : offset + EXFAT_ENTRY_SIZE]
        entry_type = entry[0]
        if entry_type == 0:
            break
        if entry_type != EXFAT_FILE_ENTRY:
            offset += EXFAT_ENTRY_SIZE
            continue

        secondary_count = entry[1]
        set_size = (secondary_count + 1) * EXFAT_ENTRY_SIZE
        if offset + set_size > len(directory) or secondary_count < 2:
            raise UnsupportedLayoutError('unsupported-layout: directory entry set spans unsupported buffer')

        stream = directory[offset + EXFAT_ENTRY_SIZE : offset + 2 * EXFAT_ENTRY_SIZE]
        if stream[0] != EXFAT_STREAM_ENTRY:
            offset += set_size
            continue

        actual_name = ''
        name_entries = secondary_count - 1
        for index in range(name_entries):
            name_entry = directory[offset + (2 + index) * EXFAT_ENTRY_SIZE : offset + (3 + index) * EXFAT_ENTRY_SIZE]
            if name_entry[0] != EXFAT_NAME_ENTRY:
                raise UnsupportedLayoutError('unsupported-layout: unsupported exfat name entry set')
            actual_name += parse_name(name_entry)

        name_length = stream[3]
        actual_name = actual_name[:name_length]
        if actual_name.lower() != name.lower():
            offset += set_size
            continue

        attributes = int.from_bytes(entry[4:6], 'little')
        is_directory = (attributes & EXFAT_ATTR_DIRECTORY) != 0
        is_file = (attributes & EXFAT_ATTR_ARCHIVE) != 0
        if want_directory and not is_directory:
            raise UnsupportedLayoutError(f'unsupported-layout: {name} is not a directory')
        if not want_directory and not is_file:
            raise UnsupportedLayoutError(f'unsupported-layout: {name} is not a file')

        flags = stream[1]
        first_cluster = int.from_bytes(stream[20:24], 'little')
        data_length = int.from_bytes(stream[24:32], 'little')
        return ExfatFile(first_cluster, data_length, (flags & EXFAT_STREAM_NO_FAT_CHAIN) != 0)

    raise UnsupportedLayoutError(f'unsupported-layout: {name} not found')


def find_boot_file(vhd: BinaryIO, info: ExfatBootRegion) -> ExfatFile:
    root = read_directory(vhd, info, info.root_cluster)
    boot_dir = find_directory_child(root, 'boot', want_directory=True)
    if not boot_dir.contiguous:
        raise UnsupportedLayoutError('unsupported-layout: /boot directory is not contiguous')

    boot_directory = read_directory(vhd, info, boot_dir.first_cluster)
    boot_file = find_directory_child(boot_directory, 'boot.bin', want_directory=False)
    if not boot_file.contiguous:
        raise UnsupportedLayoutError('unsupported-layout: /boot/boot.bin is not contiguous')
    return boot_file


def bootChecksum(boot_region_offset: int, vhd: BinaryIO, partition_offset: int) -> int:
    bytes_count = 11 * BYTES_PER_SECTOR

    vhd.seek((boot_region_offset + partition_offset) * BYTES_PER_SECTOR)
    sectors = vhd.read(bytes_count)

    checksum = 0
    for i in range(bytes_count):
        if i == 106 or i == 107 or i == 112:
            continue
        temp = 0 if checksum % 2 == 0 else 0x80000000
        checksum = (checksum >> 1) + sectors[i] + temp

    return checksum


def updateChecksum(vhd: BinaryIO, partition_offset: int):
    checksum = bootChecksum(0, vhd, partition_offset).to_bytes(4, 'little')
    vhd.seek((partition_offset + 11) * BYTES_PER_SECTOR)
    for _ in range(128):
        vhd.write(checksum)

    checksum = bootChecksum(12, vhd, partition_offset).to_bytes(4, 'little')
    vhd.seek((partition_offset + 23) * BYTES_PER_SECTOR)
    for _ in range(128):
        vhd.write(checksum)

    return None


def updateMbr():
    mbr: bytes
    with open(MBR_PATH, 'rb') as f:
        mbr = f.read()

    if len(mbr) > BYTES_PER_SECTOR:
        debugError('update mbr failed, because mbr exceeds 512 bytes', False)
        return None

    with open(VDISK_PATH, 'rb+') as f:
        # save patition table entries
        ptes = read_partition_table(f)

        # write mbr
        f.seek(0)
        f.write(mbr)

        # write patition table entries
        f.seek(MBR_PARTITION_TABLE_OFFSET)
        f.write(ptes)

    debugSuccess('update mbr successfully')
    return None


def updateDbr():
    dbr: bytes
    with open(DBR_PATH, 'rb') as f:
        dbr = f.read()

    if len(dbr) > BYTES_PER_SECTOR:
        debugError('update dbr failed, because mbr exceeds 512 bytes', False)
        return None

    jmp_code = dbr[0:3]
    boot_code = dbr[0x78:]

    with open(VDISK_PATH, 'rb+') as f:
        try:
            partition = find_exfat_partition(f)
        except UnsupportedLayoutError as error:
            debugError(f'update dbr failed, because {error}', False)
            return None

        lba = partition.lba

        # main boot region
        # jmp code
        f.seek(lba * BYTES_PER_SECTOR)
        f.write(jmp_code)

        # boot code offset = 0x78
        f.seek(lba * BYTES_PER_SECTOR + 0x78)
        f.write(boot_code)

        # backup boot region
        f.seek((lba + 12) * BYTES_PER_SECTOR)
        f.write(jmp_code)

        f.seek((lba + 12) * BYTES_PER_SECTOR + 0x78)
        f.write(boot_code)

        updateChecksum(f, lba)

        # make the parition active
        f.seek(partition.entry_offset)
        attr = 0x80
        f.write(attr.to_bytes(1, 'little'))

    debugSuccess('update dbr successfully')
    return None


def updateExDbr():
    exdbr: bytes
    with open(EXDBR_PATH, 'rb') as f:
        exdbr = f.read()

    if len(exdbr) > BYTES_PER_SECTOR * EXFAT_EXTENDED_DBR_SECTORS:
        debugError('update exdbr failed, because exdbr exceeds 4096 bytes', False)
        return None

    with open(VDISK_PATH, 'rb+') as f:
        try:
            partition = find_exfat_partition(f, require_active=True)
        except UnsupportedLayoutError as error:
            debugError(f'update exdbr failed, because {error}', False)
            return None

        lba = partition.lba

        f.seek((lba + 1) * BYTES_PER_SECTOR)
        f.write(exdbr)

        f.seek((lba + 12 + 1) * BYTES_PER_SECTOR)
        f.write(exdbr)

        updateChecksum(f, lba)

    debugSuccess('update exdbr successfully')
    return None


def updateBoot():
    with open(BOOT_PATH, 'rb') as f:
        boot = f.read()

    if len(boot) > BOOT_MAX_LOAD_BYTES:
        debugError('update boot failed, because boot.bin exceeds supported bootloader load range', False)
        return None

    with open(VDISK_PATH, 'rb+') as f:
        try:
            partition = find_exfat_partition(f, require_active=True)
            info = read_exfat_boot_region(f, partition.lba)
            boot_file = find_boot_file(f, info)
            if boot_file.data_length < len(boot):
                raise UnsupportedLayoutError('unsupported-layout: /boot/boot.bin is too small')

            boot_lba = info.cluster_lba(boot_file.first_cluster)
        except UnsupportedLayoutError as error:
            debugError(f'update boot failed, because {error}', False)
            return None

        f.seek(boot_lba * info.bytes_per_sector)
        f.write(boot)

    return None


def main(argv):
    try:
        opts, _args = getopt(
            argv,
            '',
            [
                'vdisk=',
                'with-mbr=',
                'with-dbr=',
                'with-exdbr=',
                'with-boot=',
                'sector-size=',
            ],
        )
    except GetoptError as error:
        debugError(error)
        return None

    need_update_mbr = False
    need_update_dbr = False
    need_update_boot = False
    need_update_exdbr = False
    global VDISK_PATH, MBR_PATH, DBR_PATH, EXDBR_PATH, BOOT_PATH, BYTES_PER_SECTOR
    for opt, arg in opts:
        if opt in ('--vdisk'):
            VDISK_PATH = arg
        elif opt in ('--with-mbr'):
            MBR_PATH = arg
            need_update_mbr = True
        elif opt in ('--with-dbr'):
            DBR_PATH = arg
            need_update_dbr = True
        elif opt in ('--with-boot'):
            BOOT_PATH = arg
            need_update_boot = True
        elif opt in ('--with-exdbr'):
            EXDBR_PATH = arg
            need_update_exdbr = True
        elif opt in ('--sector-size'):
            BYTES_PER_SECTOR = int(arg)

    if not os.path.exists(VDISK_PATH) or not os.path.isfile(VDISK_PATH):
        debugError('aborted, virtual disk does not exist or is not a valid file')

    if need_update_mbr:
        print(MBR_PATH)
        if not os.path.exists(MBR_PATH) or not os.path.isfile(MBR_PATH):
            debugError(
                'update mbr failed, because mbr does not exist or is not a valid file',
                False,
            )
        else:
            updateMbr()

    if need_update_dbr:
        if not os.path.exists(DBR_PATH) or not os.path.isfile(DBR_PATH):
            debugError(
                'update dbr failed, because dbr does not exist or is not a valid file',
                False,
            )
        else:
            updateDbr()

    if need_update_exdbr:
        if not os.path.exists(EXDBR_PATH) or not os.path.isfile(EXDBR_PATH):
            debugError(
                'update exdbr failed, because exdbr does not exist or is not a valid file',
                False,
            )
        else:
            updateExDbr()

    if need_update_boot:
        if not os.path.exists(BOOT_PATH) or not os.path.isfile(BOOT_PATH):
            debugError(
                'update boot failed, because boot.bin does not exist or is not a valid file',
                False,
            )
        else:
            updateBoot()

    return None


if __name__ == '__main__':
    main(sys.argv[1:])
