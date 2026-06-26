#!/usr/bin/env python3
"""Prepare or clean up the minimal TAP state for virtio-net validation."""

import argparse
import platform
import shutil
import subprocess
import sys

DEFAULT_TAP_NAME = 'bigos-tap0'


def run(command: list[str], dry_run: bool) -> int:
    printable = ' '.join(command)
    print(printable)
    if dry_run:
        return 0
    return subprocess.run(command, check=False).returncode


def ensure_linux() -> str:
    system = platform.system().lower()
    if system != 'linux':
        return f'unsupported host platform: {platform.system()}'
    if shutil.which('ip') is None:
        return 'missing required tool: ip'
    return ''


def prepare(args: argparse.Namespace) -> int:
    reason = ensure_linux()
    if reason:
        print(f'BIGOS_VIRTIO_NET_TAP_SKIPPED {reason}')
        return 2
    tap = args.name
    commands = [
        ['ip', 'tuntap', 'add', 'dev', tap, 'mode', 'tap'],
        ['ip', 'link', 'set', tap, 'up'],
    ]
    for command in commands:
        status = run(command, args.dry_run)
        if status != 0:
            print(f'BIGOS_VIRTIO_NET_TAP_FAILED command={command[0]} status={status}')
            return status
    print(f'BIGOS_VIRTIO_NET_TAP_READY name={tap}')
    print('Packet injection prerequisite: inject Ethernet frames on the TAP interface from the host side.')
    return 0


def cleanup(args: argparse.Namespace) -> int:
    reason = ensure_linux()
    if reason:
        print(f'BIGOS_VIRTIO_NET_TAP_SKIPPED {reason}')
        return 2
    status = run(['ip', 'link', 'delete', args.name], args.dry_run)
    if status != 0:
        print(f'BIGOS_VIRTIO_NET_TAP_CLEANUP_FAILED status={status}')
        return status
    print(f'BIGOS_VIRTIO_NET_TAP_CLEANED name={args.name}')
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', default=DEFAULT_TAP_NAME, help='TAP interface name')
    parser.add_argument('--dry-run', action='store_true', help='print commands without changing host networking')
    subparsers = parser.add_subparsers(dest='command', required=True)
    subparsers.add_parser('prepare', help='create or prepare the TAP interface')
    subparsers.add_parser('cleanup', help='remove the TAP interface')
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == 'prepare':
        return prepare(args)
    if args.command == 'cleanup':
        return cleanup(args)
    return 2


if __name__ == '__main__':
    sys.exit(main())
