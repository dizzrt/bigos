from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

from ..errors import StageError

BYTES_PER_SECTOR = 512
MBR_PARTITION_TABLE_OFFSET = 0x1BE
MBR_PARTITION_TABLE_SIZE = 64
PARTITION_ENTRY_SIZE = 16
EXFAT_PARTITION_TYPE = 0x07
EXFAT_EXTENDED_DBR_SECTORS = 8
EXFAT_ENTRY_SIZE = 32
EXFAT_FILE_ENTRY = 0x85
EXFAT_STREAM_ENTRY = 0xC0
EXFAT_NAME_ENTRY = 0xC1
EXFAT_ATTR_DIRECTORY = 0x10
EXFAT_ATTR_ARCHIVE = 0x20
EXFAT_STREAM_NO_FAT_CHAIN = 0x02
BOOT_MAX_LOAD_BYTES = 0x80000


@dataclass(frozen=True)
class ExfatPartition:
    entry_offset: int
    lba: int
    sectors: int
    active: bool


@dataclass(frozen=True)
class ExfatBootRegion:
    partition_lba: int
    bytes_per_sector: int
    sectors_per_cluster: int
    cluster_heap_lba: int
    root_cluster: int

    def cluster_lba(self, cluster: int) -> int:
        if cluster < 2:
            raise StageError('image patch', 'unsupported-layout: invalid exfat cluster number')
        return self.cluster_heap_lba + (cluster - 2) * self.sectors_per_cluster


@dataclass(frozen=True)
class ExfatPatchedFile:
    first_cluster: int
    data_length: int
    contiguous: bool


def read_exact(image: BinaryIO, size: int) -> bytes:
    data = image.read(size)
    if len(data) != size:
        raise StageError('image patch', 'unsupported-layout: unexpected end of disk image')
    return data


def read_partition_table(image: BinaryIO) -> bytes:
    image.seek(MBR_PARTITION_TABLE_OFFSET)
    return read_exact(image, MBR_PARTITION_TABLE_SIZE)


def find_exfat_partition(image: BinaryIO, require_active: bool = False) -> ExfatPartition:
    entries = read_partition_table(image)
    found: ExfatPartition | None = None
    for index in range(4):
        entry_offset = index * PARTITION_ENTRY_SIZE
        entry = entries[entry_offset : entry_offset + PARTITION_ENTRY_SIZE]
        active = entry[0] == 0x80
        partition_type = entry[4]
        lba = int.from_bytes(entry[8:12], 'little')
        sectors = int.from_bytes(entry[12:16], 'little')

        if partition_type != EXFAT_PARTITION_TYPE:
            continue
        if lba == 0 or sectors == 0:
            raise StageError('image patch', 'unsupported-layout: exfat partition has empty bounds')
        if require_active and not active:
            continue
        if found is not None:
            raise StageError('image patch', 'unsupported-layout: multiple exfat partitions')
        found = ExfatPartition(MBR_PARTITION_TABLE_OFFSET + entry_offset, lba, sectors, active)

    if found is None:
        raise StageError('image patch', 'unsupported-layout: no supported exfat partition found')
    return found


def read_exfat_boot_region(image: BinaryIO, partition_lba: int) -> ExfatBootRegion:
    image.seek(partition_lba * BYTES_PER_SECTOR)
    sector = read_exact(image, BYTES_PER_SECTOR)
    if sector[3:11] != b'EXFAT   ':
        raise StageError('image patch', 'unsupported-layout: partition is not exfat')

    bytes_per_sector = 1 << sector[0x6C]
    sectors_per_cluster = 1 << sector[0x6D]
    if bytes_per_sector != BYTES_PER_SECTOR:
        raise StageError('image patch', 'unsupported-layout: exfat sector size does not match patch sector size')
    if sectors_per_cluster == 0:
        raise StageError('image patch', 'unsupported-layout: invalid exfat sectors per cluster')

    partition_offset = int.from_bytes(sector[0x40:0x48], 'little')
    cluster_heap_offset = int.from_bytes(sector[0x58:0x5C], 'little')
    root_cluster = int.from_bytes(sector[0x60:0x64], 'little')
    if partition_offset not in (0, partition_lba):
        raise StageError('image patch', 'unsupported-layout: unexpected exfat partition offset')

    return ExfatBootRegion(
        partition_lba=partition_lba,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        cluster_heap_lba=partition_lba + cluster_heap_offset,
        root_cluster=root_cluster,
    )


def parse_name(entry: bytes) -> str:
    chars: list[str] = []
    for offset in range(2, EXFAT_ENTRY_SIZE, 2):
        codepoint = int.from_bytes(entry[offset : offset + 2], 'little')
        if codepoint == 0:
            break
        chars.append(chr(codepoint))
    return ''.join(chars)


def read_directory(image: BinaryIO, info: ExfatBootRegion, first_cluster: int) -> bytes:
    lba = info.cluster_lba(first_cluster)
    size = info.sectors_per_cluster * info.bytes_per_sector
    image.seek(lba * info.bytes_per_sector)
    return read_exact(image, size)


def find_directory_child(directory: bytes, name: str, want_directory: bool) -> ExfatPatchedFile:
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
            raise StageError('image patch', 'unsupported-layout: directory entry set spans unsupported buffer')

        stream = directory[offset + EXFAT_ENTRY_SIZE : offset + 2 * EXFAT_ENTRY_SIZE]
        if stream[0] != EXFAT_STREAM_ENTRY:
            offset += set_size
            continue

        actual_name = ''
        for index in range(secondary_count - 1):
            name_entry = directory[offset + (2 + index) * EXFAT_ENTRY_SIZE : offset + (3 + index) * EXFAT_ENTRY_SIZE]
            if name_entry[0] != EXFAT_NAME_ENTRY:
                raise StageError('image patch', 'unsupported-layout: unsupported exfat name entry set')
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
            raise StageError('image patch', f'unsupported-layout: {name} is not a directory')
        if not want_directory and not is_file:
            raise StageError('image patch', f'unsupported-layout: {name} is not a file')

        flags = stream[1]
        first_cluster = int.from_bytes(stream[20:24], 'little')
        data_length = int.from_bytes(stream[24:32], 'little')
        return ExfatPatchedFile(first_cluster, data_length, (flags & EXFAT_STREAM_NO_FAT_CHAIN) != 0)

    raise StageError('image patch', f'unsupported-layout: {name} not found')


def find_boot_file(image: BinaryIO, info: ExfatBootRegion) -> ExfatPatchedFile:
    root = read_directory(image, info, info.root_cluster)
    boot_dir = find_directory_child(root, 'boot', want_directory=True)
    if not boot_dir.contiguous:
        raise StageError('image patch', 'unsupported-layout: /boot directory is not contiguous')
    boot_directory = read_directory(image, info, boot_dir.first_cluster)
    boot_file = find_directory_child(boot_directory, 'boot.bin', want_directory=False)
    if not boot_file.contiguous:
        raise StageError('image patch', 'unsupported-layout: /boot/boot.bin is not contiguous')
    return boot_file


def boot_checksum(boot_region_offset: int, image: BinaryIO, partition_offset: int) -> int:
    byte_count = 11 * BYTES_PER_SECTOR
    image.seek((boot_region_offset + partition_offset) * BYTES_PER_SECTOR)
    sectors = read_exact(image, byte_count)
    checksum = 0
    for index in range(byte_count):
        if index in (106, 107, 112):
            continue
        carry = 0 if checksum % 2 == 0 else 0x80000000
        checksum = ((checksum >> 1) + sectors[index] + carry) & 0xFFFFFFFF
    return checksum


def update_checksum(image: BinaryIO, partition_offset: int) -> None:
    checksum = boot_checksum(0, image, partition_offset).to_bytes(4, 'little')
    image.seek((partition_offset + 11) * BYTES_PER_SECTOR)
    for _ in range(BYTES_PER_SECTOR // 4):
        image.write(checksum)

    checksum = boot_checksum(12, image, partition_offset).to_bytes(4, 'little')
    image.seek((partition_offset + 23) * BYTES_PER_SECTOR)
    for _ in range(BYTES_PER_SECTOR // 4):
        image.write(checksum)


def update_mbr(image_path: Path, mbr_path: Path) -> None:
    mbr = mbr_path.read_bytes()
    if len(mbr) > BYTES_PER_SECTOR:
        raise StageError('image patch', 'mbr.bin exceeds 512 bytes')
    with image_path.open('rb+') as image:
        entries = read_partition_table(image)
        image.seek(0)
        image.write(mbr)
        image.seek(MBR_PARTITION_TABLE_OFFSET)
        image.write(entries)


def update_dbr(image_path: Path, dbr_path: Path) -> None:
    dbr = dbr_path.read_bytes()
    if len(dbr) > BYTES_PER_SECTOR:
        raise StageError('image patch', 'dbr.bin exceeds 512 bytes')
    jump_code = dbr[0:3]
    boot_code = dbr[0x78:]
    with image_path.open('rb+') as image:
        partition = find_exfat_partition(image)
        lba = partition.lba
        image.seek(lba * BYTES_PER_SECTOR)
        image.write(jump_code)
        image.seek(lba * BYTES_PER_SECTOR + 0x78)
        image.write(boot_code)
        image.seek((lba + 12) * BYTES_PER_SECTOR)
        image.write(jump_code)
        image.seek((lba + 12) * BYTES_PER_SECTOR + 0x78)
        image.write(boot_code)
        update_checksum(image, lba)
        image.seek(partition.entry_offset)
        image.write(b'\x80')


def update_exdbr(image_path: Path, exdbr_path: Path) -> None:
    exdbr = exdbr_path.read_bytes()
    if len(exdbr) > BYTES_PER_SECTOR * EXFAT_EXTENDED_DBR_SECTORS:
        raise StageError('image patch', 'exdbr.bin exceeds 4096 bytes')
    with image_path.open('rb+') as image:
        partition = find_exfat_partition(image, require_active=True)
        lba = partition.lba
        image.seek((lba + 1) * BYTES_PER_SECTOR)
        image.write(exdbr)
        image.seek((lba + 13) * BYTES_PER_SECTOR)
        image.write(exdbr)
        update_checksum(image, lba)


def update_boot(image_path: Path, boot_path: Path) -> None:
    boot = boot_path.read_bytes()
    if len(boot) > BOOT_MAX_LOAD_BYTES:
        raise StageError('image patch', 'boot.bin exceeds supported bootloader load range')
    with image_path.open('rb+') as image:
        partition = find_exfat_partition(image, require_active=True)
        info = read_exfat_boot_region(image, partition.lba)
        boot_file = find_boot_file(image, info)
        if boot_file.data_length < len(boot):
            raise StageError('image patch', 'unsupported-layout: /boot/boot.bin is too small')
        boot_lba = info.cluster_lba(boot_file.first_cluster)
        image.seek(boot_lba * info.bytes_per_sector)
        image.write(boot)


def patch_image(
    image_path: Path,
    *,
    mbr_path: Path | None = None,
    dbr_path: Path | None = None,
    exdbr_path: Path | None = None,
    boot_path: Path | None = None,
) -> None:
    if not image_path.is_file():
        raise StageError('image patch', f'image does not exist or is not a file: {image_path}')
    if mbr_path is not None:
        update_mbr(image_path, mbr_path)
    if dbr_path is not None:
        update_dbr(image_path, dbr_path)
    if exdbr_path is not None:
        update_exdbr(image_path, exdbr_path)
    if boot_path is not None:
        update_boot(image_path, boot_path)


__all__ = [
    'ExfatBootRegion',
    'ExfatPartition',
    'ExfatPatchedFile',
    'find_boot_file',
    'find_exfat_partition',
    'patch_image',
    'read_exfat_boot_region',
    'update_boot',
    'update_dbr',
    'update_exdbr',
    'update_mbr',
]
