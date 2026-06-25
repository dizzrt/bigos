from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from . import core
from .errors import StageError
from .image.patch import patch_image


def _add_run_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        '--image',
        default=str(core.DEFAULT_IMAGE),
        help='image path under build/test; defaults to UEFI ESP/FAT when --boot-mode uefi',
    )
    parser.add_argument('--image-size', default='64M', help='raw image size, e.g. 64M, 128M, or bytes')
    parser.add_argument(
        '--persistent-image',
        help='attach/create an independent persistent /rw test disk as ATA primary slave',
    )
    parser.add_argument(
        '--boot-mode',
        choices=core.BOOT_MODES,
        default='uefi',
        help='boot backend image/debug path: UEFI ESP/OVMF default or explicit legacy MBR/exFAT',
    )
    parser.add_argument(
        '--emulator',
        choices=core.EMULATORS,
        default='qemu',
        help='emulator backend to launch after image generation',
    )
    parser.add_argument(
        '--display',
        help='display mode selected by emulator: bochs=sdl2|none, qemu/qemu-gdb=graphical|none',
    )
    parser.add_argument(
        '--keep-image',
        action='store_true',
        help='accepted for workflow compatibility; generated image and config are always kept for inspection',
    )
    parser.add_argument('--bochsrc', help='custom Bochs config to use instead of generating one')
    parser.add_argument('--romimage', help='optional Bochs BIOS ROM path for generated config')
    parser.add_argument('--vgaromimage', help='optional Bochs VGA BIOS ROM path for generated config')
    parser.add_argument(
        '--bochs-cpus',
        type=int,
        default=1,
        help='CPU count for generated Bochs config; defaults to 1, supports 1..8',
    )
    parser.add_argument(
        '--skip-build',
        action='store_true',
        help='consume already-built kernel and boot artifacts instead of invoking xmake build',
    )
    parser.add_argument(
        '--serial-log',
        help='COM1 output file under logs/; QEMU defaults under logs/ when omitted',
    )
    parser.add_argument(
        '--expect-serial-marker',
        help='when launching an emulator, wait until this marker appears in --serial-log',
    )
    parser.add_argument(
        '--smoke-timeout',
        type=float,
        default=10.0,
        help='seconds to wait for --expect-serial-marker before failing',
    )
    parser.add_argument(
        '--bochs-extra',
        action='append',
        default=[],
        help='extra line appended to the generated Bochs config; may be repeated',
    )
    parser.add_argument(
        '--qemu-extra',
        action='append',
        default=[],
        help='extra QEMU argument string appended after stable helper-managed arguments; may be repeated',
    )
    parser.add_argument(
        '--uefi-root-image',
        help='exFAT root image attached as primary IDE for --boot-mode uefi runtime VFS payloads',
    )
    parser.add_argument('--ovmf-code', help='x86_64 OVMF code firmware path for --boot-mode uefi')
    parser.add_argument('--ovmf-vars-template', help='OVMF vars template copied for --boot-mode uefi')
    parser.add_argument(
        '--ovmf-vars-output',
        default=str(core.DEFAULT_QEMU_UEFI_VARS),
        help='generated writable OVMF vars path for --boot-mode uefi',
    )
    parser.add_argument(
        '--no-launch',
        action='store_true',
        help='prepare and validate the image without starting an emulator',
    )


def _run_image_create(args: argparse.Namespace) -> int:
    args.no_launch = True
    return core.run(args)


def _run_image_validate(args: argparse.Namespace) -> int:
    return core.validate(args)


def _run_image_patch(args: argparse.Namespace) -> int:
    patch_image(
        Path(args.image).resolve(),
        mbr_path=Path(args.with_mbr).resolve() if args.with_mbr else None,
        dbr_path=Path(args.with_dbr).resolve() if args.with_dbr else None,
        exdbr_path=Path(args.with_exdbr).resolve() if args.with_exdbr else None,
        boot_path=Path(args.with_boot).resolve() if args.with_boot else None,
    )
    print(f'patched image: {Path(args.image).resolve()}')
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description='BigOS developer tooling')
    subparsers = parser.add_subparsers(dest='command', required=True)

    run_parser = subparsers.add_parser('run', help='build artifacts, prepare an image, and launch an emulator')
    _add_run_arguments(run_parser)
    run_parser.set_defaults(func=core.run)

    image_parser = subparsers.add_parser('image', help='create, validate, or patch boot images')
    image_subparsers = image_parser.add_subparsers(dest='image_command', required=True)

    create_parser = image_subparsers.add_parser('create', help='prepare and validate an image without launching')
    _add_run_arguments(create_parser)
    create_parser.set_defaults(func=_run_image_create)

    validate_parser = image_subparsers.add_parser('validate', help='validate a generated image layout offline')
    validate_parser.add_argument('--image', required=True, help='image path to validate')
    validate_parser.add_argument(
        '--boot-mode', choices=core.BOOT_MODES, default='legacy', help='image layout to validate'
    )
    validate_parser.set_defaults(func=_run_image_validate)

    patch_parser = image_subparsers.add_parser('patch', help='patch boot artifacts into an existing raw image')
    patch_parser.add_argument('--image', required=True, help='existing raw image path to patch')
    patch_parser.add_argument('--with-mbr', help='mbr.bin path to write while preserving partition entries')
    patch_parser.add_argument('--with-dbr', help='dbr.bin path to write into main and backup boot regions')
    patch_parser.add_argument('--with-exdbr', help='exdbr.bin path to write into main and backup boot regions')
    patch_parser.add_argument('--with-boot', help='boot.bin path to write into the existing /boot/boot.bin allocation')
    patch_parser.set_defaults(func=_run_image_patch)

    smoke_parser = subparsers.add_parser('smoke', help='runtime smoke validation commands')
    smoke_subparsers = smoke_parser.add_subparsers(dest='smoke_command', required=True)
    matrix_parser = smoke_subparsers.add_parser(
        'matrix',
        help='run the runtime smoke matrix through QEMU headless serial-marker checks',
    )
    matrix_parser.add_argument(
        '--case',
        action='append',
        default=[],
        choices=('all', *(case.case_id for case in core.RUNTIME_SMOKE_MATRIX)),
        help='matrix case id to run; may be repeated; defaults to all cases',
    )
    matrix_parser.add_argument(
        '--output',
        default=str(core.LOG_DIR / 'runtime-smoke-validation.md'),
        help='Markdown validation artifact path',
    )
    matrix_parser.add_argument(
        '--serial-log-dir',
        default=str(core.LOG_DIR / 'runtime-smoke'),
        help='directory under logs/ for per-case serial logs',
    )
    matrix_parser.add_argument(
        '--image-dir',
        default=str(core.BUILD_DIR / 'test' / 'runtime-smoke'),
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
    matrix_parser.set_defaults(func=core.runtime_smoke_matrix)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except StageError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
