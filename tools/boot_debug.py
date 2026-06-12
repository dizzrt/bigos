#!/usr/bin/env python3
"""Prepare a bootable BigOS raw image and launch it in an emulator."""

from __future__ import annotations

import argparse
import math
import os
import shlex
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
BOOT_ARTIFACT_DIR = BUILD_DIR / 'bin' / 'x86' / 'boot'
DEFAULT_IMAGE = BUILD_DIR / 'test' / 'os.raw'
DEFAULT_BOCHSRC = BUILD_DIR / 'test' / 'bochsrc.bxrc'
DEFAULT_KERNEL = BUILD_DIR / 'kernel'
DEFAULT_CPU_MODEL = 'corei7_haswell_4770'
DEFAULT_SERIAL_LOG = BUILD_DIR / 'test' / 'serial.log'
DEFAULT_QEMU_SERIAL_LOG = BUILD_DIR / 'test' / 'qemu.serial.log'
DEFAULT_QEMU_GDB_SERIAL_LOG = BUILD_DIR / 'test' / 'qemu-gdb.serial.log'
MM_SELF_TEST_SUCCESS_MARKER = 'BIGOS_MM_SELF_TEST_PASSED'
EMULATORS = ('bochs', 'qemu', 'qemu-gdb')
BOCHS_DISPLAYS = ('sdl2', 'none')
QEMU_DISPLAYS = ('graphical', 'none')
RUNTIME_SMOKE_STATUS_VALUES = ('passed', 'failed', 'skipped', 'blocked')

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
FS_SMOKE_PAYLOAD = b'BIGOS_FS_SMOKE_PAYLOAD\n'
USER_INIT_ELF = BUILD_DIR / 'bin' / 'user' / 'init.elf'
USER_INIT_ELF_MAX_BYTES = 64 * 1024
USER_INIT_ELF_PATH = '/boot/user/init.elf'
# User /bin programs packaged at the root /bin directory. The kernel VFS opens
# them by absolute path; the shell PATH lookup defaults to /bin. The same
# USER_ELF_MAX_FILE_BYTES limit is enforced for both init and execve targets.
USER_BIN_DIR = BUILD_DIR / 'bin' / 'user' / 'bin'
USER_BIN_PROGRAMS = ('sh', 'echo', 'cat')
USER_SMOKE_BIN_DIR = USER_BIN_DIR / 'smoke'
USER_SMOKE_BIN_PROGRAMS = ('args', 'env', 'out', 'errno', 'exit')
USER_BIN_MAX_BYTES = USER_INIT_ELF_MAX_BYTES

BUILD_TOOLS = (
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
    fs_smoke_clusters: int
    fs_smoke_cluster: int
    user_dir_cluster: int
    user_init_clusters: int
    user_init_cluster: int
    bin_dir_cluster: int = 0
    # Tuple of (name, first_cluster, data_length, clusters) for /bin programs.
    bin_files: tuple[tuple[str, int, int, int], ...] = ()
    smoke_bin_dir_cluster: int = 0
    # Tuple of (name, first_cluster, data_length, clusters) for /bin/smoke programs.
    smoke_bin_files: tuple[tuple[str, int, int, int], ...] = ()

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
    user_init_elf: Path | None = None
    # List of (name, Path) for user /bin programs, in packaging order.
    bin_programs: tuple[tuple[str, Path], ...] = ()
    # List of (name, Path) for optional user /bin/smoke programs.
    smoke_bin_programs: tuple[tuple[str, Path], ...] = ()


@dataclass(frozen=True)
class RuntimeSmokeCase:
    case_id: str
    title: str
    switches: tuple[str, ...]
    expected_marker: str
    timeout_seconds: float
    risk_area: str
    validation_markers: tuple[str, ...] = ()
    proc_boundary: str = ''


@dataclass(frozen=True)
class ToolAvailability:
    tool: str
    available: bool
    detail: str


@dataclass
class RuntimeSmokeResult:
    case: RuntimeSmokeCase
    status: str
    expected_marker: str
    observed_marker: str
    serial_log: Path
    timeout_seconds: float
    exit_status: str
    failed_stage: str
    skip_reason: str
    alternative_checks: str
    residual_risk: str
    observed_markers: tuple[str, ...] = ()


SMOKE_OPTIONS = (
    'mm_self_test',
    'page_fault_smoke',
    'timer_smoke',
    'keyboard_smoke',
    'scheduler_smoke',
    'scheduler_semantics_smoke',
    'blocking_smoke',
    'user_vmem_smoke',
    'syscall_smoke',
    'user_program_smoke',
    'fs_smoke',
    'user_elf_smoke',
    'userland_smoke',
)
RUNTIME_SMOKE_MATRIX = (
    RuntimeSmokeCase(
        case_id='memory-self-test',
        title='Memory self-test',
        switches=('mm_self_test',),
        expected_marker=MM_SELF_TEST_SUCCESS_MARKER,
        timeout_seconds=10.0,
        risk_area='early memory allocator and direct-map runtime self-test',
    ),
    RuntimeSmokeCase(
        case_id='timer-irq',
        title='Timer IRQ',
        switches=('timer_smoke',),
        expected_marker='BIGOS_TIMER_IRQ',
        timeout_seconds=10.0,
        risk_area='PIC/PIT IRQ0 dispatch and COM1/VGA marker emission',
    ),
    RuntimeSmokeCase(
        case_id='scheduler',
        title='Cooperative scheduler',
        switches=('scheduler_smoke',),
        expected_marker='BIGOS_SCHED_THREAD_B',
        timeout_seconds=10.0,
        risk_area='cooperative kernel-thread scheduling and context switching',
    ),
    RuntimeSmokeCase(
        case_id='scheduler-semantics',
        title='Scheduler semantics',
        switches=('scheduler_semantics_smoke',),
        expected_marker='BIGOS_SCHED_SEMANTICS_PASSED',
        timeout_seconds=15.0,
        risk_area='timer slice expiry, preemption-disable deferral, and IRQ-return scheduling',
        validation_markers=(
            'BIGOS_SCHED_SEMANTICS_START',
            'BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED',
            'BIGOS_SCHED_SEMANTICS_PREEMPTED',
            'BIGOS_SCHED_SEMANTICS_PASSED',
        ),
    ),
    RuntimeSmokeCase(
        case_id='blocking-primitives',
        title='Blocking primitives',
        switches=('blocking_smoke',),
        expected_marker='BIGOS_BLOCKING_SMOKE_PASSED',
        timeout_seconds=15.0,
        risk_area='wait queue block/wakeup, timeout sleep, and cooperative scheduler resume boundaries',
        validation_markers=(
            'BIGOS_BLOCKING_WAIT_BLOCKED',
            'BIGOS_BLOCKING_WAKE_SENT',
            'BIGOS_BLOCKING_WAIT_RESUMED',
            'BIGOS_BLOCKING_TIMEOUT_BLOCKED',
            'BIGOS_BLOCKING_TIMEOUT_EXPIRED',
            'BIGOS_BLOCKING_SMOKE_PASSED',
        ),
        proc_boundary='synthetic TTY producer; no manual keyboard input required',
    ),
    RuntimeSmokeCase(
        case_id='syscall',
        title='Syscall entry',
        switches=('syscall_smoke',),
        expected_marker='BIGOS_SYSCALL_SMOKE_PASSED',
        timeout_seconds=10.0,
        risk_area='int 0x80 entry and minimal syscall ABI',
    ),
    RuntimeSmokeCase(
        case_id='filesystem-read',
        title='Read-only filesystem',
        switches=('fs_smoke',),
        expected_marker='BIGOS_FS_EXFAT_READ_PASSED',
        timeout_seconds=20.0,
        risk_area='Legacy BIOS raw image, IDE disk, ATA PIO, and read-only exFAT path',
    ),
    RuntimeSmokeCase(
        case_id='first-user-program',
        title='First user program',
        switches=('user_program_smoke',),
        expected_marker='BIGOS_USER_EXIT',
        timeout_seconds=20.0,
        risk_area='smoke-only ring3 entry, syscall write path, and process teardown',
        proc_boundary='compiles kernel/core/proc/** and is not part of a normal boot configuration',
    ),
    RuntimeSmokeCase(
        case_id='filesystem-user-elf',
        title='Filesystem-backed user ELF',
        switches=('user_elf_smoke',),
        expected_marker='BIGOS_USER_EXIT',
        timeout_seconds=30.0,
        risk_area='read-only exFAT user ELF load plus smoke-only ring3 execution',
        proc_boundary='compiles kernel/core/proc/** and packages /boot/user/init.elf; not a normal boot configuration',
    ),
    RuntimeSmokeCase(
        case_id='default-init',
        title='Default user-space init',
        switches=(),
        expected_marker='BIGOS_USER_EXEC',
        timeout_seconds=30.0,
        risk_area='default-on normal-boot ring3 init launch via launch_init (no smoke switch)',
        validation_markers=(
            'BIGOS_INIT_ENTER',
            'BIGOS_USER_EXEC',
        ),
        proc_boundary=(
            'default build (no smoke switch); packages /boot/user/init.elf and /bin/sh, enters ring3 on normal '
            'boot, and the resident C init forks + execve /bin/sh (BIGOS_USER_EXEC); PID-1 does not exit'
        ),
    ),
    RuntimeSmokeCase(
        case_id='userland-runtime',
        title='Userland runtime (crt0/libc/shell/simple C)',
        switches=('userland_smoke',),
        expected_marker='BIGOS_USERLAND_PASSED',
        timeout_seconds=40.0,
        risk_area='crt0 arg/env passing, user libc + errno translation, stdout/stderr, exit-code probes, '
        'fork/execve/wait, shell execution, single-stage pipe, redirection, and the minimal malloc/free',
        proc_boundary=(
            'default-off userland_smoke build; packages the userland validation program as /boot/user/init.elf and '
            'runs it as PID-1 with deterministic non-interactive assertions over bounded /bin/smoke C programs '
            '(no manual stdin)'
        ),
    ),
)


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


def is_bochs_backend(emulator: str) -> bool:
    return emulator == 'bochs'


def is_qemu_backend(emulator: str) -> bool:
    return emulator in ('qemu', 'qemu-gdb')


def resolve_display(emulator: str, display: str | None) -> str:
    if is_bochs_backend(emulator):
        resolved = display or 'sdl2'
        allowed = BOCHS_DISPLAYS
    elif is_qemu_backend(emulator):
        resolved = display or 'graphical'
        allowed = QEMU_DISPLAYS
    else:
        raise StageError('argument validation', f'unsupported emulator backend: {emulator}')
    if resolved not in allowed:
        allowed_values = ', '.join(allowed)
        raise StageError(
            'argument validation',
            f'unsupported display {resolved!r} for {emulator}; expected one of: {allowed_values}',
        )
    return resolved


def check_tools(emulator: str, need_emulator: bool, need_build: bool) -> None:
    missing = [tool for tool in BUILD_TOOLS if need_build and shutil.which(tool) is None]
    if need_emulator:
        if is_bochs_backend(emulator) and shutil.which('bochs') is None:
            missing.append('bochs')
        if is_qemu_backend(emulator) and shutil.which('qemu-system-x86_64') is None:
            missing.append('qemu-system-x86_64')
    if missing:
        raise StageError('preflight', 'missing required tool(s): ' + ', '.join(missing))


def build_current_artifacts() -> None:
    run_command('kernel build', ['xmake', 'build', 'kernel'], PROJECT_ROOT)
    run_command('boot build', ['xmake', 'build', 'boot-artifacts'], PROJECT_ROOT)
    run_command('user elf build', ['xmake', 'build', 'user-init-elf'], PROJECT_ROOT)
    require_file(DEFAULT_KERNEL, 'kernel build', 'kernel ELF')
    for name, (path, max_size) in BOOT_ARTIFACTS.items():
        require_file(path, 'boot build', f'{name} artifact', max_size)


def get_artifacts(kernel: Path = DEFAULT_KERNEL) -> PreparedArtifacts:
    require_file(kernel, 'image build', 'kernel ELF')
    for name, (path, max_size) in BOOT_ARTIFACTS.items():
        require_file(path, 'image build', f'{name} artifact', max_size)
    require_file(USER_INIT_ELF, 'image build', USER_INIT_ELF_PATH, USER_INIT_ELF_MAX_BYTES)
    bin_programs: list[tuple[str, Path]] = []
    for name in USER_BIN_PROGRAMS:
        program = USER_BIN_DIR / name
        require_file(program, 'image build', f'/bin/{name}', USER_BIN_MAX_BYTES)
        bin_programs.append((name, program))
    smoke_bin_programs: list[tuple[str, Path]] = []
    smoke_bin_dir = USER_BIN_DIR / 'smoke'
    if smoke_bin_dir.exists():
        for name in USER_SMOKE_BIN_PROGRAMS:
            program = smoke_bin_dir / name
            require_file(program, 'image build', f'/bin/smoke/{name}', USER_BIN_MAX_BYTES)
            smoke_bin_programs.append((name, program))
    return PreparedArtifacts(
        kernel=kernel,
        mbr=BOOT_ARTIFACTS['mbr'][0],
        dbr=BOOT_ARTIFACTS['dbr'][0],
        exdbr=BOOT_ARTIFACTS['exdbr'][0],
        boot=BOOT_ARTIFACTS['boot'][0],
        user_init_elf=USER_INIT_ELF,
        bin_programs=tuple(bin_programs),
        smoke_bin_programs=tuple(smoke_bin_programs),
    )


def make_layout(
    image_size: int,
    boot_size: int,
    kernel_size: int,
    user_init_size: int = 0,
    bin_sizes: Sequence[tuple[str, int]] = (),
    smoke_bin_sizes: Sequence[tuple[str, int]] = (),
) -> ImageLayout:
    image_size = align_up(image_size, SECTOR_SIZE)
    total_sectors = image_size // SECTOR_SIZE
    partition_sectors = total_sectors - PARTITION_LBA
    if partition_sectors <= CLUSTER_HEAP_OFFSET:
        raise StageError('image build', 'image is too small for the fixed exFAT partition layout')

    boot_clusters = clusters_for_size(boot_size)
    kernel_clusters = clusters_for_size(kernel_size)
    fs_smoke_clusters = clusters_for_size(len(FS_SMOKE_PAYLOAD))
    kernel_cluster = BOOT_FILE_CLUSTER + boot_clusters
    fs_smoke_cluster = kernel_cluster + kernel_clusters
    user_dir_cluster = fs_smoke_cluster + fs_smoke_clusters
    user_init_clusters = clusters_for_size(user_init_size) if user_init_size > 0 else 0
    user_init_cluster = user_dir_cluster + 1
    next_cluster = (
        user_init_cluster + user_init_clusters if user_init_clusters > 0 else fs_smoke_cluster + fs_smoke_clusters
    )

    # /bin directory (single cluster) plus one contiguous run per regular program.
    # Optional smoke probes live in a nested /bin/smoke directory only when built.
    bin_dir_cluster = 0
    bin_files: list[tuple[str, int, int, int]] = []
    smoke_bin_dir_cluster = 0
    smoke_bin_files: list[tuple[str, int, int, int]] = []
    if bin_sizes or smoke_bin_sizes:
        bin_dir_cluster = next_cluster
        next_cluster += 1
        for name, size in bin_sizes:
            clusters = clusters_for_size(size)
            bin_files.append((name, next_cluster, size, clusters))
            next_cluster += clusters
        if smoke_bin_sizes:
            smoke_bin_dir_cluster = next_cluster
            next_cluster += 1
            for name, size in smoke_bin_sizes:
                clusters = clusters_for_size(size)
                smoke_bin_files.append((name, next_cluster, size, clusters))
                next_cluster += clusters

    last_cluster = next_cluster - 1
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
        fs_smoke_clusters=fs_smoke_clusters,
        fs_smoke_cluster=fs_smoke_cluster,
        user_dir_cluster=user_dir_cluster,
        user_init_clusters=user_init_clusters,
        user_init_cluster=user_init_cluster,
        bin_dir_cluster=bin_dir_cluster,
        bin_files=tuple(bin_files),
        smoke_bin_dir_cluster=smoke_bin_dir_cluster,
        smoke_bin_files=tuple(smoke_bin_files),
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
    used_clusters.extend(range(layout.fs_smoke_cluster, layout.fs_smoke_cluster + layout.fs_smoke_clusters))
    if layout.user_init_clusters > 0:
        used_clusters.append(layout.user_dir_cluster)
        used_clusters.extend(range(layout.user_init_cluster, layout.user_init_cluster + layout.user_init_clusters))
    if layout.bin_dir_cluster != 0:
        used_clusters.append(layout.bin_dir_cluster)
        for _name, first_cluster, _size, clusters in layout.bin_files:
            used_clusters.extend(range(first_cluster, first_cluster + clusters))
    if layout.smoke_bin_dir_cluster != 0:
        used_clusters.append(layout.smoke_bin_dir_cluster)
        for _name, first_cluster, _size, clusters in layout.smoke_bin_files:
            used_clusters.extend(range(first_cluster, first_cluster + clusters))
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
    user_init = read_file(artifacts.user_init_elf) if artifacts.user_init_elf is not None else b''
    bin_data = [(name, read_file(program)) for name, program in artifacts.bin_programs]
    bin_sizes = [(name, len(data)) for name, data in bin_data]
    smoke_bin_data = [(name, read_file(program)) for name, program in artifacts.smoke_bin_programs]
    smoke_bin_sizes = [(name, len(data)) for name, data in smoke_bin_data]

    layout = make_layout(image_size, len(boot), len(kernel), len(user_init), bin_sizes, smoke_bin_sizes)
    image_path.parent.mkdir(parents=True, exist_ok=True)

    with image_path.open('wb') as image:
        image.truncate(layout.image_size)
        write_at(image, 0, make_mbr(layout, mbr))

        main_region = make_boot_region(layout, dbr=dbr, exdbr=exdbr)
        backup_region = make_boot_region(layout, dbr=dbr, exdbr=exdbr)
        write_at(image, layout.partition_lba * SECTOR_SIZE, main_region)
        write_at(image, (layout.partition_lba + EXFAT_BACKUP_BOOT_OFFSET) * SECTOR_SIZE, backup_region)

        write_cluster(image, layout, BITMAP_CLUSTER, make_allocation_bitmap(layout))
        root_entries = [
            ExfatFile('boot', BOOT_DIR_CLUSTER, CLUSTER_SIZE, is_directory=True),
            ExfatFile('kernel', layout.kernel_cluster, len(kernel), is_directory=False),
        ]
        if layout.bin_dir_cluster != 0:
            root_entries.append(ExfatFile('bin', layout.bin_dir_cluster, CLUSTER_SIZE, is_directory=True))
        root = make_directory(root_entries)
        boot_entries = [
            ExfatFile('boot.bin', BOOT_FILE_CLUSTER, len(boot), is_directory=False),
            ExfatFile('kernel', layout.kernel_cluster, len(kernel), is_directory=False),
            ExfatFile('fs_smoke.txt', layout.fs_smoke_cluster, len(FS_SMOKE_PAYLOAD), is_directory=False),
        ]
        if user_init:
            boot_entries.append(ExfatFile('user', layout.user_dir_cluster, CLUSTER_SIZE, is_directory=True))
        boot_dir = make_directory(boot_entries)
        write_cluster(image, layout, ROOT_DIR_CLUSTER, root)
        write_cluster(image, layout, BOOT_DIR_CLUSTER, boot_dir)
        write_at(image, layout.cluster_lba(BOOT_FILE_CLUSTER) * SECTOR_SIZE, boot)
        write_at(image, layout.cluster_lba(layout.kernel_cluster) * SECTOR_SIZE, kernel)
        write_at(image, layout.cluster_lba(layout.fs_smoke_cluster) * SECTOR_SIZE, FS_SMOKE_PAYLOAD)
        if user_init:
            user_dir = make_directory(
                [
                    ExfatFile('init.elf', layout.user_init_cluster, len(user_init), is_directory=False),
                ]
            )
            write_cluster(image, layout, layout.user_dir_cluster, user_dir)
            write_at(image, layout.cluster_lba(layout.user_init_cluster) * SECTOR_SIZE, user_init)
        if layout.bin_dir_cluster != 0:
            bin_entries = [
                ExfatFile(name, first_cluster, size, is_directory=False)
                for (name, first_cluster, size, _clusters) in layout.bin_files
            ]
            if layout.smoke_bin_dir_cluster != 0:
                bin_entries.append(ExfatFile('smoke', layout.smoke_bin_dir_cluster, CLUSTER_SIZE, is_directory=True))
            write_cluster(image, layout, layout.bin_dir_cluster, make_directory(bin_entries))
            size_by_name = {name: len(data) for name, data in bin_data}
            data_by_name = dict(bin_data)
            for name, first_cluster, _size, _clusters in layout.bin_files:
                assert size_by_name[name] == len(data_by_name[name])
                write_at(image, layout.cluster_lba(first_cluster) * SECTOR_SIZE, data_by_name[name])
        if layout.smoke_bin_dir_cluster != 0:
            smoke_entries = [
                ExfatFile(name, first_cluster, size, is_directory=False)
                for (name, first_cluster, size, _clusters) in layout.smoke_bin_files
            ]
            write_cluster(image, layout, layout.smoke_bin_dir_cluster, make_directory(smoke_entries))
            smoke_size_by_name = {name: len(data) for name, data in smoke_bin_data}
            smoke_data_by_name = dict(smoke_bin_data)
            for name, first_cluster, _size, _clusters in layout.smoke_bin_files:
                assert smoke_size_by_name[name] == len(smoke_data_by_name[name])
                write_at(image, layout.cluster_lba(first_cluster) * SECTOR_SIZE, smoke_data_by_name[name])

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
        fs_smoke_entry = find_child(boot_directory, 'fs_smoke.txt', want_directory=False)
        if boot_file_entry.data_length <= 0:
            raise StageError('image validate', '/boot/boot.bin is empty')
        if kernel_entry.data_length <= 0:
            raise StageError('image validate', 'kernel is empty')
        if boot_dir_kernel_entry.first_cluster != kernel_entry.first_cluster:
            raise StageError('image validate', '/boot/kernel does not point at the root kernel data')
        image.seek(cluster_lba(fs_smoke_entry.first_cluster) * SECTOR_SIZE)
        fs_smoke_payload = image.read(fs_smoke_entry.data_length)
        if fs_smoke_payload != FS_SMOKE_PAYLOAD:
            raise StageError('image validate', '/boot/fs_smoke.txt payload is invalid')

        try:
            user_dir_entry = find_child(boot_directory, 'user', want_directory=True)
        except StageError:
            user_dir_entry = None
        if user_dir_entry is not None:
            image.seek(cluster_lba(user_dir_entry.first_cluster) * SECTOR_SIZE)
            user_directory = image.read(CLUSTER_SIZE)
            init_entry = find_child(user_directory, 'init.elf', want_directory=False)
            if init_entry.data_length <= 0:
                raise StageError('image validate', f'{USER_INIT_ELF_PATH} is empty')
            if init_entry.data_length > USER_INIT_ELF_MAX_BYTES:
                raise StageError('image validate', f'{USER_INIT_ELF_PATH} exceeds the loader bound')

        try:
            bin_dir_entry = find_child(root, 'bin', want_directory=True)
        except StageError:
            bin_dir_entry = None
        if bin_dir_entry is not None:
            image.seek(cluster_lba(bin_dir_entry.first_cluster) * SECTOR_SIZE)
            bin_directory = image.read(CLUSTER_SIZE)
            for name in USER_BIN_PROGRAMS:
                if not (USER_BIN_DIR / name).is_file():
                    continue
                entry = find_child(bin_directory, name, want_directory=False)
                if entry.data_length <= 0:
                    raise StageError('image validate', f'/bin/{name} is empty')
                if entry.data_length > USER_BIN_MAX_BYTES:
                    raise StageError('image validate', f'/bin/{name} exceeds the loader bound')


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
    display: str,
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
        f'display_library: {"nogui" if display == "none" else display}',
        'boot: disk',
        'ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14',
        f'ata0-master: type=disk, path="{image_path}", mode=flat, '
        f'cylinders={cylinders}, heads={heads}, spt={sectors_per_track}, sect_size=512',
        serial_line,
        'log: build/test/bochs.log',
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


def split_extra_args(extra_args: Sequence[str]) -> list[str]:
    result: list[str] = []
    for item in extra_args:
        result.extend(shlex.split(item))
    return result


def qemu_command(
    image_path: Path,
    serial_log: Path,
    display: str,
    *,
    gdb: bool = False,
    extra_args: Sequence[str] = (),
) -> list[str]:
    command = [
        'qemu-system-x86_64',
        '-drive',
        f'file={image_path},format=raw,if=ide',
        '-boot',
        'c',
        '-serial',
        f'file:{serial_log}',
        '-no-reboot',
        '-no-shutdown',
    ]
    if display == 'none':
        command.extend(['-display', 'none'])
    if gdb:
        command.extend(['-S', '-s'])
    command.extend(split_extra_args(extra_args))
    return command


def launch_qemu(command: Sequence[str], *, gdb: bool = False) -> None:
    if gdb:
        print('qemu gdb stub: target remote localhost:1234')
    run_command('qemu launch', command, PROJECT_ROOT)


def launch_qemu_until_serial_marker(
    command: Sequence[str],
    serial_log: Path,
    marker: str,
    timeout_seconds: float,
) -> None:
    if serial_log.exists():
        serial_log.unlink()
    serial_log.parent.mkdir(parents=True, exist_ok=True)

    printable = ' '.join(command)
    log_stage(f'qemu smoke: {printable}')
    process = subprocess.Popen(
        command,
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
                raise StageError('qemu smoke', f'QEMU exited before marker {marker!r}\n{output}')

            time.sleep(0.1)

        stop_process_group(process)
        raise StageError('qemu smoke', f'timed out waiting for serial marker {marker!r} in {serial_log}')
    finally:
        stop_process_group(process)


def read_observed_marker(serial_log: Path, expected_marker: str) -> str:
    if not serial_log.is_file():
        return ''
    contents = serial_log.read_text(encoding='utf-8', errors='replace')
    if expected_marker in contents:
        return expected_marker
    for case in RUNTIME_SMOKE_MATRIX:
        if case.expected_marker in contents:
            return case.expected_marker
    for token in contents.split():
        if token.startswith('BIGOS_'):
            return token
    return ''


def read_observed_markers(serial_log: Path, markers: Sequence[str]) -> tuple[str, ...]:
    if not markers or not serial_log.is_file():
        return ()
    contents = serial_log.read_text(encoding='utf-8', errors='replace')
    return tuple(marker for marker in markers if marker in contents)


def default_serial_log_for(emulator: str) -> Path | None:
    if emulator == 'qemu':
        return DEFAULT_QEMU_SERIAL_LOG
    if emulator == 'qemu-gdb':
        return DEFAULT_QEMU_GDB_SERIAL_LOG
    return None


def run(args: argparse.Namespace) -> int:
    image_path = Path(args.image).resolve()
    bochsrc_path = Path(args.bochsrc).resolve() if args.bochsrc else DEFAULT_BOCHSRC
    image_size = parse_size(args.image_size)
    should_launch = not args.no_launch
    emulator = args.emulator
    display = resolve_display(emulator, args.display)
    default_serial_log = default_serial_log_for(emulator)
    serial_log = Path(args.serial_log).resolve() if args.serial_log else default_serial_log
    marker = args.expect_serial_marker

    check_tools(emulator, need_emulator=should_launch, need_build=not args.skip_build)
    if not args.skip_build:
        build_current_artifacts()
    artifacts = get_artifacts(DEFAULT_KERNEL)

    log_stage(f'image build: {image_path}')
    layout = create_image(image_path, image_size, artifacts)
    validate_image(image_path)

    if is_bochs_backend(emulator):
        bochs_extra = list(args.bochs_extra)
        if args.bochsrc:
            log_stage(f'using custom Bochs config: {bochsrc_path}')
        else:
            render_bochsrc(image_path, bochsrc_path, args.romimage, args.vgaromimage, serial_log, display, bochs_extra)

    print(f'image: {image_path}')
    print(f'emulator: {emulator}')
    if is_bochs_backend(emulator):
        print(f'bochsrc: {bochsrc_path}')
    if serial_log:
        print(f'serial_log: {serial_log}')
    if is_qemu_backend(emulator) and serial_log:
        print(
            'qemu_command: '
            + ' '.join(
                qemu_command(
                    image_path,
                    serial_log,
                    display,
                    gdb=emulator == 'qemu-gdb',
                    extra_args=args.qemu_extra,
                )
            )
        )
    print(f'partition_lba: {layout.partition_lba}')
    print(f'cluster_heap_lba: {layout.cluster_heap_lba}')

    if should_launch:
        if is_bochs_backend(emulator):
            cleanup_image_lock(image_path)
            if marker and serial_log:
                launch_bochs_until_serial_marker(bochsrc_path, serial_log, marker, args.smoke_timeout)
            else:
                launch_bochs(bochsrc_path)
        else:
            assert serial_log is not None
            serial_log.parent.mkdir(parents=True, exist_ok=True)
            command = qemu_command(
                image_path,
                serial_log,
                display,
                gdb=emulator == 'qemu-gdb',
                extra_args=args.qemu_extra,
            )
            if marker:
                launch_qemu_until_serial_marker(command, serial_log, marker, args.smoke_timeout)
            else:
                launch_qemu(command, gdb=emulator == 'qemu-gdb')
    else:
        log_stage(f'{emulator} launch skipped by --no-launch')
    return 0


def validate(args: argparse.Namespace) -> int:
    validate_image(Path(args.image).resolve())
    print(f'validated image: {Path(args.image).resolve()}')
    return 0


def case_by_id(case_id: str) -> RuntimeSmokeCase:
    for case in RUNTIME_SMOKE_MATRIX:
        if case.case_id == case_id:
            return case
    raise StageError('runtime smoke matrix', f'unknown runtime smoke case: {case_id}')


def selected_runtime_smoke_cases(case_ids: Sequence[str]) -> list[RuntimeSmokeCase]:
    if not case_ids or 'all' in case_ids:
        return list(RUNTIME_SMOKE_MATRIX)
    seen: set[str] = set()
    cases: list[RuntimeSmokeCase] = []
    for case_id in case_ids:
        if case_id in seen:
            continue
        seen.add(case_id)
        cases.append(case_by_id(case_id))
    return cases


def runtime_smoke_xmake_config(case: RuntimeSmokeCase) -> list[str]:
    enabled = set(case.switches)
    return ['xmake', 'f', *(f'--{option}={"y" if option in enabled else "n"}' for option in SMOKE_OPTIONS)]


def runtime_smoke_serial_log(case: RuntimeSmokeCase, serial_log_dir: Path) -> Path:
    return serial_log_dir / f'{case.case_id}.serial.log'


def runtime_smoke_run_args(
    case: RuntimeSmokeCase,
    image_path: Path,
    serial_log: Path,
    image_size: str,
) -> argparse.Namespace:
    return argparse.Namespace(
        image=str(image_path),
        image_size=image_size,
        emulator='qemu',
        display='none',
        keep_image=True,
        bochsrc=None,
        romimage=None,
        vgaromimage=None,
        skip_build=False,
        serial_log=str(serial_log),
        expect_serial_marker=case.expected_marker,
        smoke_timeout=case.timeout_seconds,
        bochs_extra=[],
        qemu_extra=[],
        no_launch=False,
    )


def collect_tool_availability(include_bochs: bool) -> list[ToolAvailability]:
    tools = ['uv', *BUILD_TOOLS, 'qemu-system-x86_64']
    if include_bochs:
        tools.append('bochs')
    availability: list[ToolAvailability] = []
    for tool in tools:
        resolved = shutil.which(tool)
        availability.append(ToolAvailability(tool=tool, available=resolved is not None, detail=resolved or 'missing'))
    return availability


def missing_required_tools(availability: Sequence[ToolAvailability]) -> list[str]:
    required = {
        'uv',
        'xmake',
        'x86_64-elf-gcc',
        'x86_64-elf-g++',
        'x86_64-elf-ld',
        'x86_64-elf-as',
        'qemu-system-x86_64',
    }
    return [item.tool for item in availability if item.tool in required and not item.available]


def markdown_escape(value: str) -> str:
    return value.replace('|', r'\|').replace('\n', '<br>')


def format_runtime_smoke_artifact(
    results: Sequence[RuntimeSmokeResult],
    availability: Sequence[ToolAvailability],
    selected_cases: Sequence[RuntimeSmokeCase],
    *,
    stopped_after_failure: bool,
    bochs_note: str,
) -> str:
    lines = [
        '# Runtime Smoke Validation',
        '',
        '## Schema Fields',
        '',
        '- `schema_version`: `runtime-smoke-validation/v1`',
        '- `preferred_emulator`: `qemu`',
        '- `preferred_display`: `none`',
        '- `image_path`: existing Legacy BIOS/MBR/exFAT raw image path generated by `tools/boot_debug.py`',
        '- `case.status`: one of `passed`, `failed`, `skipped`, or `blocked`',
        '- `case.observed_markers`: optional intermediate `BIGOS_` markers observed for multi-step cases',
        '',
        '## Tool Availability',
        '',
        '| Tool | Available | Detail |',
        '| --- | --- | --- |',
    ]
    for item in availability:
        lines.append(f'| `{item.tool}` | `{str(item.available).lower()}` | `{markdown_escape(item.detail)}` |')

    lines.extend(
        [
            '',
            '## Runtime Smoke Matrix',
            '',
            '| Case | Switches | Expected marker | Timeout | Preferred path | Proc boundary |',
            '| --- | --- | --- | ---: | --- | --- |',
        ]
    )
    for case in selected_cases:
        switches = ' '.join(f'--{switch}=y' for switch in case.switches)
        boundary = case.proc_boundary or 'normal default-off smoke boundary'
        lines.append(
            '| '
            + ' | '.join(
                [
                    f'`{case.case_id}`',
                    f'`xmake f {switches}`',
                    f'`{case.expected_marker}`',
                    f'`{case.timeout_seconds:g}s`',
                    '`tools/boot_debug.py run --emulator qemu --display none`',
                    markdown_escape(boundary),
                ]
            )
            + ' |'
        )

    lines.extend(
        [
            '',
            '## Case Results',
            '',
            '| Case | Status | Expected marker | Observed marker | Serial log | Timeout | Exit status | Failed stage |',
            '| --- | --- | --- | --- | --- | ---: | --- | --- |',
        ]
    )
    for result in results:
        lines.append(
            '| '
            + ' | '.join(
                [
                    f'`{result.case.case_id}`',
                    f'`{result.status}`',
                    f'`{result.expected_marker}`',
                    f'`{result.observed_marker or ""}`',
                    f'`{result.serial_log}`',
                    f'`{result.timeout_seconds:g}s`',
                    markdown_escape(result.exit_status),
                    markdown_escape(result.failed_stage),
                ]
            )
            + ' |'
        )

    lines.extend(['', '## Skips Blocks And Risks', ''])
    for result in results:
        lines.extend(
            [
                f'### `{result.case.case_id}`',
                '',
                f'- `status`: `{result.status}`',
                f'- `skip_reason`: {result.skip_reason or "none"}',
                f'- `alternative_checks`: {result.alternative_checks or "none"}',
                f'- `residual_risk`: {result.residual_risk or "none"}',
                f'- `risk_area`: {result.case.risk_area}',
                f'- `observed_markers`: {", ".join(result.observed_markers) or "none"}',
                '',
            ]
        )

    lines.extend(
        [
            '## Low-Level Cross-Validation',
            '',
            '- Bochs or QEMU+Bochs cross-validation remains scenario-specific for boot, IRQ, timer, '
            'keyboard IRQ, ATA PIO, port IO, and hardware-behavior changes.',
            f'- Bochs status: {bochs_note}',
            '- If Bochs, ROM paths, display configuration, or host setup are unavailable, record the skipped '
            'backend, substitute QEMU/build/source checks, and residual hardware-behavior risk.',
            '',
            '## Non-Goals',
            '',
            '- This validation productization does not add OS runtime features, CI platform integration, UEFI '
            'support, storage drivers, or new smoke marker ABI.',
            '- Existing smoke switches remain default-off outside explicit `xmake f ...=y` configuration.',
            '- Existing boot layout, disk image layout, interrupt ABI, syscall ABI, and smoke-only user process '
            'boundaries remain unchanged.',
        ]
    )
    if stopped_after_failure:
        lines.extend(['', '> Matrix stopped after the first failed case because `--keep-going` was not set.'])
    return '\n'.join(lines) + '\n'


def write_runtime_smoke_artifact(
    path: Path,
    results: Sequence[RuntimeSmokeResult],
    availability: Sequence[ToolAvailability],
    selected_cases: Sequence[RuntimeSmokeCase],
    *,
    stopped_after_failure: bool,
    bochs_note: str,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        format_runtime_smoke_artifact(
            results,
            availability,
            selected_cases,
            stopped_after_failure=stopped_after_failure,
            bochs_note=bochs_note,
        ),
        encoding='utf-8',
    )


def blocked_runtime_smoke_result(case: RuntimeSmokeCase, serial_log: Path, reason: str) -> RuntimeSmokeResult:
    return RuntimeSmokeResult(
        case=case,
        status='blocked',
        expected_marker=case.expected_marker,
        observed_marker='',
        serial_log=serial_log,
        timeout_seconds=case.timeout_seconds,
        exit_status='not run',
        failed_stage='preflight',
        skip_reason=reason,
        alternative_checks='tool availability was recorded in the validation artifact',
        residual_risk='runtime marker was not observed',
    )


def run_runtime_smoke_case(case: RuntimeSmokeCase, args: argparse.Namespace) -> RuntimeSmokeResult:
    serial_log_dir = Path(args.serial_log_dir).resolve()
    serial_log = runtime_smoke_serial_log(case, serial_log_dir)
    image_path = Path(args.image_dir).resolve() / f'{case.case_id}.raw'

    try:
        run_command('runtime smoke config', runtime_smoke_xmake_config(case), PROJECT_ROOT)
        run(runtime_smoke_run_args(case, image_path, serial_log, args.image_size))
    except StageError as error:
        observed_marker = read_observed_marker(serial_log, case.expected_marker)
        observed_markers = read_observed_markers(serial_log, case.validation_markers)
        return RuntimeSmokeResult(
            case=case,
            status='failed',
            expected_marker=case.expected_marker,
            observed_marker=observed_marker,
            serial_log=serial_log,
            timeout_seconds=case.timeout_seconds,
            exit_status=error.message,
            failed_stage=error.stage,
            skip_reason='',
            alternative_checks=(
                'inspect xmake output and serial log; rerun this single case manually after fixing the failure'
            ),
            residual_risk='expected serial marker was not observed',
            observed_markers=observed_markers,
        )

    return RuntimeSmokeResult(
        case=case,
        status='passed',
        expected_marker=case.expected_marker,
        observed_marker=read_observed_marker(serial_log, case.expected_marker),
        serial_log=serial_log,
        timeout_seconds=case.timeout_seconds,
        exit_status='0',
        failed_stage='',
        skip_reason='',
        alternative_checks='',
        residual_risk='',
        observed_markers=read_observed_markers(serial_log, case.validation_markers),
    )


def runtime_smoke_matrix(args: argparse.Namespace) -> int:
    selected_cases = selected_runtime_smoke_cases(args.case)
    output_path = Path(args.output).resolve()
    availability = collect_tool_availability(include_bochs=args.record_bochs)
    missing_tools = missing_required_tools(availability)
    bochs = next((item for item in availability if item.tool == 'bochs'), None)
    bochs_note = 'available' if bochs and bochs.available else 'not checked'
    if bochs and not bochs.available:
        bochs_note = 'unavailable; cross-validation should record substitute checks and residual hardware risk'

    results: list[RuntimeSmokeResult] = []
    stopped_after_failure = False
    if missing_tools:
        reason = 'missing required tool(s): ' + ', '.join(missing_tools)
        for case in selected_cases:
            serial_log = runtime_smoke_serial_log(case, Path(args.serial_log_dir))
            results.append(blocked_runtime_smoke_result(case, serial_log, reason))
    else:
        for case in selected_cases:
            log_stage(f'runtime smoke matrix: {case.case_id}')
            result = run_runtime_smoke_case(case, args)
            results.append(result)
            if result.status == 'failed' and not args.keep_going:
                stopped_after_failure = True
                break

    write_runtime_smoke_artifact(
        output_path,
        results,
        availability,
        selected_cases,
        stopped_after_failure=stopped_after_failure,
        bochs_note=bochs_note,
    )
    print(f'runtime_smoke_validation: {output_path}')
    return 1 if any(result.status in ('failed', 'blocked') for result in results) else 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest='command', required=True)

    run_parser = subparsers.add_parser(
        'run',
        help='build artifacts, prepare a raw image, and launch an emulator',
    )
    run_parser.add_argument(
        '--image',
        default=str(DEFAULT_IMAGE),
        help='raw disk image path under build/test by default',
    )
    run_parser.add_argument('--image-size', default='64M', help='raw image size, e.g. 64M, 128M, or bytes')
    run_parser.add_argument(
        '--emulator',
        choices=EMULATORS,
        default='bochs',
        help='emulator backend to launch after image generation',
    )
    run_parser.add_argument(
        '--display',
        help='display mode selected by emulator: bochs=sdl2|none, qemu/qemu-gdb=graphical|none',
    )
    run_parser.add_argument(
        '--keep-image',
        action='store_true',
        help='accepted for workflow compatibility; generated image and config are always kept for inspection',
    )
    run_parser.add_argument('--bochsrc', help='custom Bochs config to use instead of generating one')
    run_parser.add_argument('--romimage', help='optional Bochs BIOS ROM path for generated config')
    run_parser.add_argument('--vgaromimage', help='optional Bochs VGA BIOS ROM path for generated config')
    run_parser.add_argument(
        '--skip-build',
        action='store_true',
        help='consume already-built kernel and boot artifacts instead of invoking xmake build',
    )
    run_parser.add_argument(
        '--serial-log',
        help='COM1 output file; QEMU defaults under build/test when omitted',
    )
    run_parser.add_argument(
        '--expect-serial-marker',
        help='when launching an emulator, wait until this marker appears in --serial-log',
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
        '--qemu-extra',
        action='append',
        default=[],
        help='extra QEMU argument string appended after stable helper-managed arguments; may be repeated',
    )
    run_parser.add_argument(
        '--no-launch',
        action='store_true',
        help='prepare and validate the image without starting an emulator',
    )
    run_parser.set_defaults(func=run)

    validate_parser = subparsers.add_parser('validate-image', help='validate a generated raw image layout offline')
    validate_parser.add_argument('--image', required=True, help='raw image path to validate')
    validate_parser.set_defaults(func=validate)

    matrix_parser = subparsers.add_parser(
        'runtime-smoke-matrix',
        help='run the stage 9 runtime smoke matrix through QEMU headless serial-marker checks',
    )
    matrix_parser.add_argument(
        '--case',
        action='append',
        default=[],
        choices=('all', *(case.case_id for case in RUNTIME_SMOKE_MATRIX)),
        help='matrix case id to run; may be repeated; defaults to all cases',
    )
    matrix_parser.add_argument(
        '--output',
        default=str(BUILD_DIR / 'test' / 'runtime-smoke-validation.md'),
        help='Markdown validation artifact path',
    )
    matrix_parser.add_argument(
        '--serial-log-dir',
        default=str(BUILD_DIR / 'test' / 'runtime-smoke'),
        help='directory for per-case serial logs',
    )
    matrix_parser.add_argument(
        '--image-dir',
        default=str(BUILD_DIR / 'test' / 'runtime-smoke'),
        help='directory for per-case raw disk images',
    )
    matrix_parser.add_argument('--image-size', default='64M', help='raw image size for each case')
    matrix_parser.add_argument(
        '--keep-going',
        action='store_true',
        help='continue after a failed case instead of stopping at the first failure',
    )
    matrix_parser.add_argument(
        '--record-bochs',
        action='store_true',
        help='include Bochs availability in the artifact for scenario-specific cross-validation notes',
    )
    matrix_parser.set_defaults(func=runtime_smoke_matrix)
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
