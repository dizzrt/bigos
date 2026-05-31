#!/usr/bin/env python3
import os
import sys
from getopt import GetoptError, getopt
from typing import BinaryIO

VDISK_PATH = '../../../build/os.vhd'
MBR_PATH = ''
DBR_PATH = ''
EXDBR_PATH = ''
BOOT_PATH = ''

BYTES_PER_SECTOR = 512


def debugSuccess(msg: object):
    print('\033[32m' + str(msg) + '\033[0m')


def debugError(msg: object, abort: bool = True):
    print('\033[31merror:' + str(msg) + '\033[0m')
    if abort:
        exit(1)
    return None


def debugWarn(msg: object):
    print('\033[93m' + str(msg) + '\033[0m')


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

    if len(mbr) > 0x200:
        debugError('update mbr failed, because mbr exceeds 512 bytes', False)
        return None

    with open(VDISK_PATH, 'rb+') as f:
        # save patition table entries
        f.seek(0x1BE)
        ptes = f.read(64)

        # write mbr
        f.seek(0)
        f.write(mbr)

        # write patition table entries
        f.seek(0x1BE)
        f.write(ptes)

    debugSuccess('update mbr successfully')
    return None


def updateDbr():
    dbr: bytes
    with open(DBR_PATH, 'rb') as f:
        dbr = f.read()

    if len(dbr) > 0x200:
        debugError('update dbr failed, because mbr exceeds 512 bytes', False)
        return None

    jmp_code = dbr[0:3]
    boot_code = dbr[0x78:]

    with open(VDISK_PATH, 'rb+') as f:
        f.seek(0x1BE)
        ptes = f.read(16)

        pte_offset = 0
        for _ in range(4):
            # search exfat partition
            type_offset = pte_offset + 0x04
            partition_type = int.from_bytes(ptes[type_offset : type_offset + 1], 'little')
            if partition_type == 0x07:
                break
            pte_offset = pte_offset + 0x10

        if pte_offset > 0x30:
            debugError('update dbr failed, because no exfat parition was found', False)
            return None

        # lba of partition start
        lba_offset = pte_offset + 0x08
        lba = int.from_bytes(ptes[lba_offset : lba_offset + 4], 'little')

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
        f.seek(pte_offset + 0x1BE)
        attr = ptes[pte_offset] | 0x80
        f.write(attr.to_bytes(1, 'little'))

    debugSuccess('update dbr successfully')
    return None


def updateExDbr():
    exdbr: bytes
    with open(EXDBR_PATH, 'rb') as f:
        exdbr = f.read()

    if len(exdbr) > 0x200 * 8:
        debugError('update exdbr failed, because exdbr exceeds 4096 bytes', False)
        return None

    with open(VDISK_PATH, 'rb+') as f:
        f.seek(0x1BE)
        ptes = f.read(16)

        pte_offset = 0
        for _ in range(4):
            is_active = ptes[pte_offset] & 0x80
            type_offset = pte_offset + 0x04
            partition_type = int.from_bytes(ptes[type_offset : type_offset + 1], 'little')
            if partition_type == 0x07 and is_active == 0x80:
                break
            pte_offset = pte_offset + 0x10

        if pte_offset > 0x30:
            debugError('update dbr failed, because no exfat parition was found', False)
            return None

        lba_offset = pte_offset + 0x08
        lba = int.from_bytes(ptes[lba_offset : lba_offset + 4], 'little')

        f.seek((lba + 1) * BYTES_PER_SECTOR)
        f.write(exdbr)

        f.seek((lba + 12 + 1) * BYTES_PER_SECTOR)
        f.write(exdbr)

        updateChecksum(f, lba)

    debugSuccess('update exdbr successfully')
    return None


def updateBoot():
    debugWarn('update bootloader is not supported yet')
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
                'update boot failed, because mbr does not exist or is not a valid file',
                False,
            )
        else:
            updateBoot()

    return None


if __name__ == '__main__':
    main(sys.argv[1:])
