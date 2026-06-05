#!/usr/bin/env python3
"""Build a bootable BigOS raw image and launch it in Bochs."""

from __future__ import annotations

import argparse
import math
import os
import shutil
import signal
import subprocess
import sys
import time
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

PROJECT_ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = PROJECT_ROOT / 'build'
BOOT_DIR = PROJECT_ROOT / 'src' / 'arch' / 'x86' / 'boot'
BOOT_ARTIFACT_DIR = BUILD_DIR / 'bin' / 'x86' / 'boot'
DEFAULT_IMAGE = BUILD_DIR / 'test' / 'os.raw'
DEFAULT_BOCHSRC = BUILD_DIR / 'test' / 'bochsrc.bxrc'
DEFAULT_KERNEL = BUILD_DIR / 'kernel'
DEFAULT_CPU_MODEL = 'corei7_haswell_4770'
DEFAULT_SERIAL_LOG = BUILD_DIR / 'test' / 'serial.log'
MM_SELF_TEST_SUCCESS_MARKER = 'BIGOS_MM_SELF_TEST_PASSED'

SECTOR_SIZE = 512
MBR_PARTITION_TABLE_OFFSET = 0x1BE
MBR_SIGNATURE_OFFSET = 0x1FE
PARTITION_ENTRY_SIZE = 16
EXFAT_PARTITION_TYPE = 0x07
EXFAT_MAIN_BOOT_SECTORS = 12
EXFAT_BACKUP_BOOT_OFFSET = 12
EXFAT_TOTAL_BOOT_SECTORS = 24
EXFAT_EXTENDED_DBR_SECTORS = 8
EXFAT_ENTRY_SIZE = 32
EXFAT_FILE_ENTRY = 0x85
EXFAT_STREAM_ENTRY = 0xC0
EXFAT_NAME_ENTRY = 0xC1
EXFAT_ATTR_DIRECTORY = 0x10
EXFAT_ATTR_ARCHIVE = 0x20
EXFAT_STREAM_NO_FAT_CHAIN = 0x02
BOOT_MAX_LOAD_BYTES = 0x80000

DEFAULT_IMAGE_SIZE = 64 * 1024 * 1024
PARTITION_LBA = 2048
SECTORS_PER_CLUSTER = 8
CLUSTER_SIZE = SECTOR_SIZE * SECTORS_PER_CLUSTER
FAT_OFFSET = EXFAT_TOTAL_BOOT_SECTORS
FAT_SECTORS = 8
CLUSTER_HEAP_OFFSET = FAT_OFFSET + FAT_SECTORS
BITMAP_CLUSTER = 2
ROOT_DIR_CLUSTER = 3
BOOT_DIR_CLUSTER = 4
BOOT_FILE_CLUSTER = 5

PRE_FLIGHT_TOOLS = (
    'python3',
    'xmake',
    'x86_64-elf-gcc',
    'x86_64-elf-g++',
    'x86_64-elf-ld',
    'x86_64-elf-as',
)
BOOT_ARTIFACTS = {
    'mbr': (BOOT_ARTIFACT_DIR / 'mbr.bin', SECTOR_SIZE),
    'dbr': (BOOT_ARTIFACT_DIR / 'dbr.bin', SECTOR_SIZE),
    'exdbr': (BOOT_ARTIFACT_DIR / 'exdbr.bin', SECTOR_SIZE * EXFAT_EXTENDED_DBR_SECTORS),
    'boot': (BOOT_ARTIFACT_DIR / 'boot.bin', BOOT_MAX_LOAD_BYTES),
}


class StageError(RuntimeError):
    def __init__(self, stage: str, message: str):
        super().__init__(f'[{stage}] {message}')
        self.stage = stage
        self.message = message


@dataclass(frozen=True)
class ImageLayout:
    image_size: int
    total_sectors: int
    partition_lba: int
    partition_sectors: int
    cluster_count: int
    boot_file_clusters: int
    kernel_clusters: int
    kernel_cluster: int

    @property
    def cluster_heap_lba(self) -> int:
        return self.partition_lba + CLUSTER_HEAP_OFFSET

    def cluster_lba(self, cluster: int) -> int:
        if cluster < 2:
            raise StageError('image build', f'invalid exFAT cluster number: {cluster}')
        return self.cluster_heap_lba + (cluster - 2) * SECTORS_PER_CLUSTER


@dataclass(frozen=True)
class ExfatFile:
    name: str
    first_cluster: int
    data_length: int
    is_directory: bool = False


@dataclass(frozen=True)
class PreparedArtifacts:
    kernel: Path
    mbr: Path
    dbr: Path
    exdbr: Path
    boot: Path


def log_stage(message: str) -> None:
    print(f'==> {message}', flush=True)


def parse_size(value: str) -> int:
    text = value.strip().lower()
    multipliers = {
        'k': 1024,
        'kb': 1024,
        'm': 1024**2,
        'mb': 1024**2,
        'g': 1024**3,
        'gb': 1024**3,
    }
    for suffix, multiplier in sorted(multipliers.items(), key=lambda item: len(item[0]), reverse=True):
        if text.endswith(suffix):
            number = text[: -len(suffix)]
            return int(number) * multiplier
    return int(text)


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def clusters_for_size(size: int) -> int:
    return max(1, align_up(size, CLUSTER_SIZE) // CLUSTER_SIZE)


def require_file(path: Path, stage: str, description: str, max_size: int | None = None) -> None:
    if not path.is_file():
        raise StageError(stage, f'missing {description}: {path}')
    if max_size is not None:
        size = path.stat().st_size
        if size > max_size:
            raise StageError(stage, f'{description} is too large: {size} bytes > {max_size} bytes')


def run_command(
    stage: str,
    command: Sequence[str],
    cwd: Path,
    *,
    capture_output: bool = False,
    allow_result: Callable[[subprocess.CompletedProcess[str] | subprocess.CompletedProcess[bytes]], bool] | None = None,
) -> None:
    printable = ' '.join(command)
    log_stage(f'{stage}: {printable}')
    if capture_output:
        result = subprocess.run(
            command,
            cwd=cwd,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.stdout:
            print(result.stdout, end='' if result.stdout.endswith('\n') else '\n')
    else:
        result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode != 0 and not (allow_result and allow_result(result)):
        raise StageError(stage, f'command failed with exit code {result.returncode}: {printable}')


def check_tools(need_bochs: bool) -> None:
    missing = [tool for tool in PRE_FLIGHT_TOOLS if shutil.which(tool) is None]
    if need_bochs and shutil.which('bochs') is None:
        missing.append('bochs')
    if missing:
        raise StageError('preflight', 'missing required tool(s): ' + ', '.join(missing))


def build_kernel(memory_self_test: bool) -> None:
    run_command(
        'kernel config',
        ['xmake', 'f', f'--mm_self_test={"y" if memory_self_test else "n"}'],
        PROJECT_ROOT,
    )
    run_command('kernel build', ['xmake'], PROJECT_ROOT)
    require_file(DEFAULT_KERNEL, 'kernel build', 'kernel ELF')


def build_boot_artifacts() -> None:
    run_command(
        'boot build',
        ['make', '-C', str(BOOT_DIR), 'build-mbr', 'build-dbr', 'build-exdbr', 'build-boot'],
        PROJECT_ROOT,
    )
    for name, (path, max_size) in BOOT_ARTIFACTS.items():
        require_file(path, 'boot build', f'{name} artifact', max_size)


def get_artifacts(kernel: Path = DEFAULT_KERNEL) -> PreparedArtifacts:
    require_file(kernel, 'image build', 'kernel ELF')
    for name, (path, max_size) in BOOT_ARTIFACTS.items():
        require_file(path, 'image build', f'{name} artifact', max_size)
    return PreparedArtifacts(
        kernel=kernel,
        mbr=BOOT_ARTIFACTS['mbr'][0],
        dbr=BOOT_ARTIFACTS['dbr'][0],
        exdbr=BOOT_ARTIFACTS['exdbr'][0],
        boot=BOOT_ARTIFACTS['boot'][0],
    )


def make_layout(image_size: int, boot_size: int, kernel_size: int) -> ImageLayout:
    image_size = align_up(image_size, SECTOR_SIZE)
    total_sectors = image_size // SECTOR_SIZE
    partition_sectors = total_sectors - PARTITION_LBA
    if partition_sectors <= CLUSTER_HEAP_OFFSET:
        raise StageError('image build', 'image is too small for the fixed exFAT partition layout')

    boot_clusters = clusters_for_size(boot_size)
    kernel_clusters = clusters_for_size(kernel_size)
    kernel_cluster = BOOT_FILE_CLUSTER + boot_clusters
    last_cluster = kernel_cluster + kernel_clusters - 1
    cluster_count = (partition_sectors - CLUSTER_HEAP_OFFSET) // SECTORS_PER_CLUSTER
    if cluster_count < last_cluster - 1:
        raise StageError(
            'image build',
            'image is too small for boot.bin and kernel; increase --image-size',
        )
    return ImageLayout(
        image_size=image_size,
        total_sectors=total_sectors,
        partition_lba=PARTITION_LBA,
        partition_sectors=partition_sectors,
        cluster_count=cluster_count,
        boot_file_clusters=boot_clusters,
        kernel_clusters=kernel_clusters,
        kernel_cluster=kernel_cluster,
    )


def boot_checksum(region: bytes) -> int:
    if len(region) != 11 * SECTOR_SIZE:
        raise StageError('image build', 'exFAT boot checksum input must cover 11 sectors')
    checksum = 0
    for index, value in enumerate(region):
        if index in (106, 107, 112):
            continue
        carry = 0 if checksum % 2 == 0 else 0x80000000
        checksum = ((checksum >> 1) + value + carry) & 0xFFFFFFFF
    return checksum


def make_checksum_sector(checksum: int) -> bytes:
    return checksum.to_bytes(4, 'little') * (SECTOR_SIZE // 4)


def make_exfat_boot_sector(layout: ImageLayout) -> bytearray:
    sector = bytearray(SECTOR_SIZE)
    sector[0:3] = b'\xeb\x76\x90'
    sector[3:11] = b'EXFAT   '
    sector[0x40:0x48] = layout.partition_lba.to_bytes(8, 'little')
    sector[0x48:0x50] = layout.partition_sectors.to_bytes(8, 'little')
    sector[0x50:0x54] = FAT_OFFSET.to_bytes(4, 'little')
    sector[0x54:0x58] = FAT_SECTORS.to_bytes(4, 'little')
    sector[0x58:0x5C] = CLUSTER_HEAP_OFFSET.to_bytes(4, 'little')
    sector[0x5C:0x60] = layout.cluster_count.to_bytes(4, 'little')
    sector[0x60:0x64] = ROOT_DIR_CLUSTER.to_bytes(4, 'little')
    sector[0x64:0x68] = (0xB1605).to_bytes(4, 'little')
    sector[0x68:0x6A] = (0x0100).to_bytes(2, 'little')
    sector[0x6A:0x6C] = (0).to_bytes(2, 'little')
    sector[0x6C] = int(math.log2(SECTOR_SIZE))
    sector[0x6D] = int(math.log2(SECTORS_PER_CLUSTER))
    sector[0x6E] = 1
    sector[0x6F] = 0x80
    sector[0x70] = 1
    sector[0x1FE:0x200] = b'\x55\xaa'
    return sector


def make_boot_region(layout: ImageLayout, dbr: bytes | None = None, exdbr: bytes | None = None) -> bytes:
    region = bytearray(EXFAT_MAIN_BOOT_SECTORS * SECTOR_SIZE)
    region[0:SECTOR_SIZE] = make_exfat_boot_sector(layout)
    if dbr is not None:
        region[0:3] = dbr[0:3]
        region[0x78 : 0x78 + len(dbr[0x78:])] = dbr[0x78:]
    if exdbr is not None:
        region[SECTOR_SIZE : SECTOR_SIZE + len(exdbr)] = exdbr
    checksum = boot_checksum(bytes(region[: 11 * SECTOR_SIZE]))
    region[11 * SECTOR_SIZE : 12 * SECTOR_SIZE] = make_checksum_sector(checksum)
    return bytes(region)


def make_partition_entry(layout: ImageLayout) -> bytes:
    entry = bytearray(PARTITION_ENTRY_SIZE)
    entry[0] = 0x80
    entry[1:4] = b'\xff\xff\xff'
    entry[4] = EXFAT_PARTITION_TYPE
    entry[5:8] = b'\xff\xff\xff'
    entry[8:12] = layout.partition_lba.to_bytes(4, 'little')
    entry[12:16] = layout.partition_sectors.to_bytes(4, 'little')
    return bytes(entry)


def make_mbr(layout: ImageLayout, mbr_code: bytes) -> bytes:
    if len(mbr_code) > SECTOR_SIZE:
        raise StageError('image build', 'mbr.bin exceeds 512 bytes')
    sector = bytearray(SECTOR_SIZE)
    sector[: len(mbr_code)] = mbr_code
    sector[MBR_PARTITION_TABLE_OFFSET : MBR_PARTITION_TABLE_OFFSET + PARTITION_ENTRY_SIZE] = make_partition_entry(
        layout
    )
    sector[MBR_SIGNATURE_OFFSET : MBR_SIGNATURE_OFFSET + 2] = b'\x55\xaa'
    return bytes(sector)


def exfat_name_hash(name: str) -> int:
    checksum = 0
    for char in name.upper().encode('utf-16le'):
        checksum = (((checksum & 1) << 15) | (checksum >> 1)) + char
        checksum &= 0xFFFF
    return checksum


def make_file_entry(file: ExfatFile) -> bytes:
    name_utf16 = file.name.encode('utf-16le')
    name_entries = max(1, math.ceil(len(file.name) / 15))
    entry = bytearray((2 + name_entries) * EXFAT_ENTRY_SIZE)

    entry[0] = EXFAT_FILE_ENTRY
    entry[1] = 1 + name_entries
    attributes = EXFAT_ATTR_DIRECTORY if file.is_directory else EXFAT_ATTR_ARCHIVE
    entry[4:6] = attributes.to_bytes(2, 'little')

    stream_offset = EXFAT_ENTRY_SIZE
    entry[stream_offset] = EXFAT_STREAM_ENTRY
    entry[stream_offset + 1] = EXFAT_STREAM_NO_FAT_CHAIN
    entry[stream_offset + 3] = len(file.name)
    entry[stream_offset + 4 : stream_offset + 6] = exfat_name_hash(file.name).to_bytes(2, 'little')
    entry[stream_offset + 8 : stream_offset + 16] = file.data_length.to_bytes(8, 'little')
    entry[stream_offset + 20 : stream_offset + 24] = file.first_cluster.to_bytes(4, 'little')
    entry[stream_offset + 24 : stream_offset + 32] = file.data_length.to_bytes(8, 'little')

    for index in range(name_entries):
        name_offset = (2 + index) * EXFAT_ENTRY_SIZE
        chunk = name_utf16[index * 30 : (index + 1) * 30]
        entry[name_offset] = EXFAT_NAME_ENTRY
        entry[name_offset + 2 : name_offset + 2 + len(chunk)] = chunk
    return bytes(entry)


def make_directory(entries: Iterable[ExfatFile]) -> bytes:
    directory = bytearray(CLUSTER_SIZE)
    offset = 0
    for entry in entries:
        raw = make_file_entry(entry)
        if offset + len(raw) > len(directory):
            raise StageError('image build', 'fixed exFAT directory cluster is full')
        directory[offset : offset + len(raw)] = raw
        offset += len(raw)
    return bytes(directory)


def make_allocation_bitmap(layout: ImageLayout) -> bytes:
    bitmap = bytearray(CLUSTER_SIZE)
    used_clusters = [BITMAP_CLUSTER, ROOT_DIR_CLUSTER, BOOT_DIR_CLUSTER]
    used_clusters.extend(range(BOOT_FILE_CLUSTER, BOOT_FILE_CLUSTER + layout.boot_file_clusters))
    used_clusters.extend(range(layout.kernel_cluster, layout.kernel_cluster + layout.kernel_clusters))
    for cluster in used_clusters:
        index = cluster - 2
        bitmap[index // 8] |= 1 << (index % 8)
    return bytes(bitmap)


def write_at(image: BinaryIO, offset: int, data: bytes) -> None:
    image.seek(offset)
    image.write(data)


def write_cluster(image: BinaryIO, layout: ImageLayout, cluster: int, data: bytes) -> None:
    if len(data) > CLUSTER_SIZE * clusters_for_size(len(data)):
        raise StageError('image build', f'cluster data is too large for cluster {cluster}')
    start = layout.cluster_lba(cluster) * SECTOR_SIZE
    write_at(image, start, data)


def read_file(path: Path) -> bytes:
    with path.open('rb') as file:
        return file.read()


def create_image(image_path: Path, image_size: int, artifacts: PreparedArtifacts) -> ImageLayout:
    boot = read_file(artifacts.boot)
    kernel = read_file(artifacts.kernel)
    mbr = read_file(artifacts.mbr)
    dbr = read_file(artifacts.dbr)
    exdbr = read_file(artifacts.exdbr)

    layout = make_layout(image_size, len(boot), len(kernel))
    image_path.parent.mkdir(parents=True, exist_ok=True)

    with image_path.open('wb') as image:
        image.truncate(layout.image_size)
        write_at(image, 0, make_mbr(layout, mbr))

        main_region = make_boot_region(layout, dbr=dbr, exdbr=exdbr)
        backup_region = make_boot_region(layout, dbr=dbr, exdbr=exdbr)
        write_at(image, layout.partition_lba * SECTOR_SIZE, main_region)
        write_at(image, (layout.partition_lba + EXFAT_BACKUP_BOOT_OFFSET) * SECTOR_SIZE, backup_region)

        write_cluster(image, layout, BITMAP_CLUSTER, make_allocation_bitmap(layout))
        root = make_directory(
            [
                ExfatFile('boot', BOOT_DIR_CLUSTER, CLUSTER_SIZE, is_directory=True),
                ExfatFile('kernel', layout.kernel_cluster, len(kernel), is_directory=False),
            ]
        )
        boot_dir = make_directory(
            [
                ExfatFile('boot.bin', BOOT_FILE_CLUSTER, len(boot), is_directory=False),
                ExfatFile('kernel', layout.kernel_cluster, len(kernel), is_directory=False),
            ]
        )
        write_cluster(image, layout, ROOT_DIR_CLUSTER, root)
        write_cluster(image, layout, BOOT_DIR_CLUSTER, boot_dir)
        write_at(image, layout.cluster_lba(BOOT_FILE_CLUSTER) * SECTOR_SIZE, boot)
        write_at(image, layout.cluster_lba(layout.kernel_cluster) * SECTOR_SIZE, kernel)

    return layout


def parse_directory_name(entry: bytes) -> str:
    chars: list[str] = []
    for offset in range(2, EXFAT_ENTRY_SIZE, 2):
        value = int.from_bytes(entry[offset : offset + 2], 'little')
        if value == 0:
            break
        chars.append(chr(value))
    return ''.join(chars)


def find_child(directory: bytes, name: str, want_directory: bool) -> ExfatFile:
    offset = 0
    while offset + EXFAT_ENTRY_SIZE <= len(directory):
        entry = directory[offset : offset + EXFAT_ENTRY_SIZE]
        if entry[0] == 0:
            break
        if entry[0] != EXFAT_FILE_ENTRY:
            offset += EXFAT_ENTRY_SIZE
            continue
        secondary_count = entry[1]
        set_size = (secondary_count + 1) * EXFAT_ENTRY_SIZE
        stream = directory[offset + EXFAT_ENTRY_SIZE : offset + 2 * EXFAT_ENTRY_SIZE]
        if secondary_count < 2 or stream[0] != EXFAT_STREAM_ENTRY:
            offset += max(set_size, EXFAT_ENTRY_SIZE)
            continue
        actual_name = ''
        for index in range(secondary_count - 1):
            name_entry = directory[offset + (2 + index) * EXFAT_ENTRY_SIZE : offset + (3 + index) * EXFAT_ENTRY_SIZE]
            if name_entry[0] != EXFAT_NAME_ENTRY:
                raise StageError('image validate', 'unsupported exFAT name entry set')
            actual_name += parse_directory_name(name_entry)
        actual_name = actual_name[: stream[3]]
        if actual_name.lower() != name.lower():
            offset += set_size
            continue
        attributes = int.from_bytes(entry[4:6], 'little')
        is_directory = (attributes & EXFAT_ATTR_DIRECTORY) != 0
        if want_directory != is_directory:
            raise StageError('image validate', f'{name} has the wrong directory/file type')
        flags = stream[1]
        if (flags & EXFAT_STREAM_NO_FAT_CHAIN) == 0:
            raise StageError('image validate', f'{name} is not marked contiguous')
        return ExfatFile(
            actual_name,
            int.from_bytes(stream[20:24], 'little'),
            int.from_bytes(stream[24:32], 'little'),
            is_directory=is_directory,
        )

    raise StageError('image validate', f'{name} not found')


def validate_image(image_path: Path) -> None:
    with image_path.open('rb') as image:
        mbr = image.read(SECTOR_SIZE)
        if mbr[MBR_SIGNATURE_OFFSET : MBR_SIGNATURE_OFFSET + 2] != b'\x55\xaa':
            raise StageError('image validate', 'MBR signature is missing')
        pte = mbr[MBR_PARTITION_TABLE_OFFSET : MBR_PARTITION_TABLE_OFFSET + PARTITION_ENTRY_SIZE]
        if pte[0] != 0x80 or pte[4] != EXFAT_PARTITION_TYPE:
            raise StageError('image validate', 'active exFAT partition entry is missing')
        partition_lba = int.from_bytes(pte[8:12], 'little')

        image.seek(partition_lba * SECTOR_SIZE)
        boot_sector = image.read(SECTOR_SIZE)
        if boot_sector[3:11] != b'EXFAT   ':
            raise StageError('image validate', 'main exFAT boot sector OEM name is invalid')
        bytes_per_sector = 1 << boot_sector[0x6C]
        sectors_per_cluster = 1 << boot_sector[0x6D]
        if bytes_per_sector != SECTOR_SIZE or sectors_per_cluster != SECTORS_PER_CLUSTER:
            raise StageError('image validate', 'exFAT sector or cluster size is incompatible')
        cluster_heap_lba = partition_lba + int.from_bytes(boot_sector[0x58:0x5C], 'little')
        root_cluster = int.from_bytes(boot_sector[0x60:0x64], 'little')

        image.seek((partition_lba + EXFAT_BACKUP_BOOT_OFFSET) * SECTOR_SIZE)
        backup_sector = image.read(SECTOR_SIZE)
        if backup_sector[3:11] != b'EXFAT   ':
            raise StageError('image validate', 'backup exFAT boot sector OEM name is invalid')

        def cluster_lba(cluster: int) -> int:
            return cluster_heap_lba + (cluster - 2) * SECTORS_PER_CLUSTER

        image.seek(cluster_lba(root_cluster) * SECTOR_SIZE)
        root = image.read(CLUSTER_SIZE)
        boot_dir_entry = find_child(root, 'boot', want_directory=True)
        kernel_entry = find_child(root, 'kernel', want_directory=False)
        image.seek(cluster_lba(boot_dir_entry.first_cluster) * SECTOR_SIZE)
        boot_directory = image.read(CLUSTER_SIZE)
        boot_file_entry = find_child(boot_directory, 'boot.bin', want_directory=False)
        boot_dir_kernel_entry = find_child(boot_directory, 'kernel', want_directory=False)
        if boot_file_entry.data_length <= 0:
            raise StageError('image validate', '/boot/boot.bin is empty')
        if kernel_entry.data_length <= 0:
            raise StageError('image validate', 'kernel is empty')
        if boot_dir_kernel_entry.first_cluster != kernel_entry.first_cluster:
            raise StageError('image validate', '/boot/kernel does not point at the root kernel data')


def disk_geometry(image_size: int) -> tuple[int, int, int]:
    total_sectors = image_size // SECTOR_SIZE
    for heads in (16, 8, 4, 2, 1):
        for sectors_per_track in (63, 32, 16, 8, 4, 2, 1):
            sectors_per_cylinder = heads * sectors_per_track
            if total_sectors % sectors_per_cylinder == 0:
                return total_sectors // sectors_per_cylinder, heads, sectors_per_track
    return total_sectors, 1, 1


def render_bochsrc(
    image_path: Path,
    output_path: Path,
    romimage: str | None,
    vgaromimage: str | None,
    serial_log: Path | None,
    extra_lines: Sequence[str],
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    serial_line = 'com1: enabled=1, mode=null'
    if serial_log is not None:
        serial_log.parent.mkdir(parents=True, exist_ok=True)
        serial_line = f'com1: enabled=1, mode=file, dev="{serial_log}"'
    image_size = image_path.stat().st_size
    cylinders, heads, sectors_per_track = disk_geometry(image_size)
    lines = [
        'memory: host=32, guest=32',
        f'cpu: model={DEFAULT_CPU_MODEL}, count=1',
        'boot: disk',
        'ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14',
        f'ata0-master: type=disk, path="{image_path}", mode=flat, '
        f'cylinders={cylinders}, heads={heads}, spt={sectors_per_track}, sect_size=512',
        serial_line,
        'log: -',
        'panic: action=fatal',
        'error: action=report',
        'info: action=report',
        'debug: action=ignore',
    ]
    if romimage:
        lines.insert(0, f'romimage: file="{romimage}"')
    if vgaromimage:
        lines.insert(1 if romimage else 0, f'vgaromimage: file="{vgaromimage}"')
    lines.extend(extra_lines)
    output_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def image_lock_path(image_path: Path) -> Path:
    return image_path.with_name(f'{image_path.name}.lock')


def cleanup_image_lock(image_path: Path) -> None:
    lock_path = image_lock_path(image_path)
    if not lock_path.exists():
        return
    log_stage(f'removing stale image lock: {lock_path}')
    lock_path.unlink()


def is_bochs_user_shutdown(
    result: subprocess.CompletedProcess[str] | subprocess.CompletedProcess[bytes],
) -> bool:
    output = result.stdout or ''
    if isinstance(output, bytes):
        output = output.decode(errors='replace')
    return result.returncode == 1 and 'User requested shutdown.' in output


def launch_bochs(bochsrc: Path) -> None:
    run_command(
        'bochs launch',
        ['bochs', '-f', str(bochsrc), '-q'],
        PROJECT_ROOT,
        capture_output=True,
        allow_result=is_bochs_user_shutdown,
    )


def launch_bochs_until_serial_marker(bochsrc: Path, serial_log: Path, marker: str, timeout_seconds: float) -> None:
    if serial_log.exists():
        serial_log.unlink()

    def stop_process_group(process: subprocess.Popen[str]) -> None:
        if process.poll() is not None:
            return
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        try:
            process.wait(timeout=5)
            return
        except subprocess.TimeoutExpired:
            pass

        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            return
        process.wait(timeout=5)

    printable = f'bochs -f {bochsrc} -q'
    log_stage(f'bochs smoke: {printable}')
    process = subprocess.Popen(
        ['bochs', '-f', str(bochsrc), '-q'],
        cwd=PROJECT_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    deadline = time.monotonic() + timeout_seconds
    try:
        while time.monotonic() < deadline:
            if serial_log.exists() and marker in serial_log.read_text(encoding='utf-8', errors='replace'):
                stop_process_group(process)
                print(f'serial marker observed: {marker}')
                return

            if process.poll() is not None:
                output = process.stdout.read() if process.stdout is not None else ''
                raise StageError('bochs smoke', f'Bochs exited before marker {marker!r}\n{output}')

            time.sleep(0.1)

        stop_process_group(process)
        raise StageError('bochs smoke', f'timed out waiting for serial marker {marker!r} in {serial_log}')
    finally:
        stop_process_group(process)


def run(args: argparse.Namespace) -> int:
    image_path = Path(args.image).resolve()
    bochsrc_path = Path(args.bochsrc).resolve() if args.bochsrc else DEFAULT_BOCHSRC
    image_size = parse_size(args.image_size)
    should_launch = not args.no_launch
    serial_log = Path(args.serial_log).resolve() if args.serial_log else None
    marker = args.expect_serial_marker

    if args.memory_self_test:
        serial_log = serial_log or DEFAULT_SERIAL_LOG
        marker = marker or MM_SELF_TEST_SUCCESS_MARKER

    check_tools(need_bochs=should_launch)
    build_kernel(args.memory_self_test)
    build_boot_artifacts()
    artifacts = get_artifacts(DEFAULT_KERNEL)

    log_stage(f'image build: {image_path}')
    layout = create_image(image_path, image_size, artifacts)
    validate_image(image_path)

    if args.bochsrc:
        log_stage(f'using custom Bochs config: {bochsrc_path}')
    else:
        render_bochsrc(image_path, bochsrc_path, args.romimage, args.vgaromimage, serial_log, args.bochs_extra)

    print(f'image: {image_path}')
    print(f'bochsrc: {bochsrc_path}')
    if serial_log:
        print(f'serial_log: {serial_log}')
    print(f'partition_lba: {layout.partition_lba}')
    print(f'cluster_heap_lba: {layout.cluster_heap_lba}')

    if should_launch:
        cleanup_image_lock(image_path)
        if marker and serial_log:
            launch_bochs_until_serial_marker(bochsrc_path, serial_log, marker, args.smoke_timeout)
        else:
            launch_bochs(bochsrc_path)
    else:
        log_stage('bochs launch skipped by --no-launch')
    return 0


def validate(args: argparse.Namespace) -> int:
    validate_image(Path(args.image).resolve())
    print(f'validated image: {Path(args.image).resolve()}')
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest='command', required=True)

    run_parser = subparsers.add_parser(
        'run',
        help='build artifacts, prepare a raw image, and launch Bochs',
    )
    run_parser.add_argument(
        '--image',
        default=str(DEFAULT_IMAGE),
        help='raw disk image path under build/test by default',
    )
    run_parser.add_argument('--image-size', default='64M', help='raw image size, e.g. 64M, 128M, or bytes')
    run_parser.add_argument(
        '--keep-image',
        action='store_true',
        help='accepted for workflow compatibility; generated image and config are always kept for inspection',
    )
    run_parser.add_argument('--bochsrc', help='custom Bochs config to use instead of generating one')
    run_parser.add_argument('--romimage', help='optional Bochs BIOS ROM path for generated config')
    run_parser.add_argument('--vgaromimage', help='optional Bochs VGA BIOS ROM path for generated config')
    run_parser.add_argument(
        '--memory-self-test',
        action='store_true',
        help='build with BIGOS_MM_SELF_TEST and route COM1 to a serial log',
    )
    run_parser.add_argument(
        '--serial-log',
        help='COM1 output file for generated Bochs config; defaults under build/test for memory self-test',
    )
    run_parser.add_argument(
        '--expect-serial-marker',
        help='when launching Bochs, wait until this marker appears in --serial-log',
    )
    run_parser.add_argument(
        '--smoke-timeout',
        type=float,
        default=10.0,
        help='seconds to wait for --expect-serial-marker before failing',
    )
    run_parser.add_argument(
        '--bochs-extra',
        action='append',
        default=[],
        help='extra line appended to the generated Bochs config; may be repeated',
    )
    run_parser.add_argument(
        '--no-launch',
        action='store_true',
        help='prepare and validate the image without starting Bochs',
    )
    run_parser.set_defaults(func=run)

    validate_parser = subparsers.add_parser('validate-image', help='validate a generated raw image layout offline')
    validate_parser.add_argument('--image', required=True, help='raw image path to validate')
    validate_parser.set_defaults(func=validate)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except StageError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
