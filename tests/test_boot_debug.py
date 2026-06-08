import importlib.util
import sys
from io import StringIO
from pathlib import Path

import pytest

MODULE_PATH = Path(__file__).resolve().parents[1] / 'tools' / 'boot_debug.py'
PROJECT_ROOT = MODULE_PATH.parents[1]
spec = importlib.util.spec_from_file_location('boot_debug', MODULE_PATH)
assert spec is not None
boot_debug = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = boot_debug
spec.loader.exec_module(boot_debug)


def write_bytes(path: Path, data: bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def make_artifacts(tmp_path: Path):
    mbr = bytearray(boot_debug.SECTOR_SIZE)
    mbr[0:3] = b'MBR'
    mbr[boot_debug.MBR_SIGNATURE_OFFSET : boot_debug.MBR_SIGNATURE_OFFSET + 2] = b'\x55\xaa'

    dbr = bytearray(boot_debug.SECTOR_SIZE)
    dbr[0:3] = b'\xeb\x76\x90'
    dbr[0x78:0x7B] = b'DBR'
    dbr[boot_debug.MBR_SIGNATURE_OFFSET : boot_debug.MBR_SIGNATURE_OFFSET + 2] = b'\x55\xaa'

    return boot_debug.PreparedArtifacts(
        kernel=write_bytes(tmp_path / 'kernel', b'kernel-image' * 64),
        mbr=write_bytes(tmp_path / 'mbr.bin', bytes(mbr)),
        dbr=write_bytes(tmp_path / 'dbr.bin', bytes(dbr)),
        exdbr=write_bytes(tmp_path / 'exdbr.bin', b'EXDBR' + bytes(1024)),
        boot=write_bytes(tmp_path / 'boot.bin', b'boot-loader' * 32),
    )


def test_parse_size_suffixes() -> None:
    assert boot_debug.parse_size('64M') == 64 * 1024 * 1024
    assert boot_debug.parse_size('2kb') == 2 * 1024
    assert boot_debug.parse_size('512') == 512


def test_disk_geometry_matches_default_image_size() -> None:
    cylinders, heads, sectors_per_track = boot_debug.disk_geometry(boot_debug.DEFAULT_IMAGE_SIZE)

    assert cylinders * heads * sectors_per_track * boot_debug.SECTOR_SIZE == boot_debug.DEFAULT_IMAGE_SIZE
    assert (heads, sectors_per_track) == (16, 32)


def test_create_image_and_validate_layout(tmp_path: Path) -> None:
    image = tmp_path / 'os.raw'
    layout = boot_debug.create_image(image, boot_debug.DEFAULT_IMAGE_SIZE, make_artifacts(tmp_path))

    assert image.stat().st_size == boot_debug.DEFAULT_IMAGE_SIZE
    assert layout.partition_lba == boot_debug.PARTITION_LBA
    boot_debug.validate_image(image)


def test_create_image_can_package_user_init_elf(tmp_path: Path) -> None:
    artifacts = make_artifacts(tmp_path)
    artifacts = boot_debug.PreparedArtifacts(
        kernel=artifacts.kernel,
        mbr=artifacts.mbr,
        dbr=artifacts.dbr,
        exdbr=artifacts.exdbr,
        boot=artifacts.boot,
        user_init_elf=write_bytes(tmp_path / 'init.elf', b'\x7fELF' + bytes(128)),
    )
    image = tmp_path / 'os.raw'

    layout = boot_debug.create_image(image, boot_debug.DEFAULT_IMAGE_SIZE, artifacts)

    assert layout.user_init_clusters == 1
    assert layout.user_init_cluster > layout.user_dir_cluster
    boot_debug.validate_image(image)


def test_generated_bochsrc_is_sanitized(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(image, bochsrc, None, None, None, 'sdl2', [])

    contents = bochsrc.read_text(encoding='utf-8')
    assert f'path="{image}"' in contents
    assert 'boot: disk' in contents
    assert f'cpu: model={boot_debug.DEFAULT_CPU_MODEL}, count=1' in contents
    assert 'heads=16, spt=32' in contents
    assert 'cpuid:' not in contents
    assert 'win32' not in contents.lower()
    assert 'C:\\' not in contents


def test_generated_bochsrc_defaults_to_sdl2_display(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(image, bochsrc, None, None, None, boot_debug.resolve_display('bochs', None), [])

    contents = bochsrc.read_text(encoding='utf-8')
    assert 'display_library: sdl2' in contents


def test_generated_bochsrc_can_select_sdl2_display_explicitly(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(image, bochsrc, None, None, None, boot_debug.resolve_display('bochs', 'sdl2'), [])

    contents = bochsrc.read_text(encoding='utf-8')
    assert 'display_library: sdl2' in contents


def test_generated_bochsrc_can_select_no_gui_display(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(image, bochsrc, None, None, None, boot_debug.resolve_display('bochs', 'none'), [])

    contents = bochsrc.read_text(encoding='utf-8')
    assert 'display_library: nogui' in contents


def test_bochs_extra_can_override_generated_display(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(
        image,
        bochsrc,
        None,
        None,
        None,
        boot_debug.resolve_display('bochs', 'sdl2'),
        ['display_library: nogui'],
    )

    contents = bochsrc.read_text(encoding='utf-8')
    assert contents.index('display_library: sdl2') < contents.index('display_library: nogui')


def test_qemu_command_uses_legacy_bios_ide_disk_and_headless_serial(tmp_path: Path) -> None:
    image = tmp_path / 'os.raw'
    serial_log = tmp_path / 'qemu.serial.log'

    command = boot_debug.qemu_command(image, serial_log, 'none')

    assert command[0] == 'qemu-system-x86_64'
    assert '-drive' in command
    assert f'file={image},format=raw,if=ide' in command
    assert command[command.index('-boot') + 1] == 'c'
    assert command[command.index('-serial') + 1] == f'file:{serial_log}'
    assert command[command.index('-display') + 1] == 'none'
    assert '-no-reboot' in command
    assert '-no-shutdown' in command
    assert not any('virtio' in part or 'ahci' in part or 'ovmf' in part.lower() for part in command)


def test_qemu_gdb_command_pauses_before_boot_and_keeps_graphical_default(tmp_path: Path) -> None:
    command = boot_debug.qemu_command(
        tmp_path / 'os.raw',
        tmp_path / 'qemu-gdb.serial.log',
        'graphical',
        gdb=True,
        extra_args=['-d int'],
    )

    assert '-S' in command
    assert '-s' in command
    assert '-display' not in command
    assert command[-2:] == ['-d', 'int']


def test_cleanup_image_lock_removes_image_sidecar_lock(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', b'image')
    lock = tmp_path / 'os.raw.lock'
    lock.write_text('stale lock', encoding='utf-8')

    boot_debug.cleanup_image_lock(image)

    assert boot_debug.image_lock_path(image) == lock
    assert not lock.exists()


def test_build_current_artifacts_uses_saved_xmake_config(monkeypatch) -> None:
    commands: list[list[str]] = []

    def fake_run_command(stage, command, cwd, **kwargs):
        commands.append(list(command))

    monkeypatch.setattr(boot_debug, 'run_command', fake_run_command)
    monkeypatch.setattr(boot_debug, 'require_file', lambda *args, **kwargs: None)

    boot_debug.build_current_artifacts()

    assert commands == [
        ['xmake', 'build', 'kernel'],
        ['xmake', 'build', 'boot-artifacts'],
        ['xmake', 'build', 'user-init-elf'],
    ]


def test_run_parser_rejects_smoke_shortcuts() -> None:
    parser = boot_debug.make_parser()

    for shortcut in ('--memory-self-test', '--user-program-smoke'):
        try:
            parser.parse_args(['run', shortcut])
        except SystemExit as error:
            assert error.code == 2
        else:
            raise AssertionError(f'{shortcut} should be rejected')


def test_run_parser_rejects_removed_bochs_sdl2_backend() -> None:
    parser = boot_debug.make_parser()

    with pytest.raises(SystemExit) as error:
        parser.parse_args(['run', '--emulator', 'bochs-sdl2'])

    assert error.value.code == 2


def test_run_parser_accepts_skip_build() -> None:
    parser = boot_debug.make_parser()

    args = parser.parse_args(['run', '--skip-build', '--emulator', 'qemu', '--display', 'none'])

    assert args.skip_build is True
    assert args.emulator == 'qemu'
    assert args.display == 'none'


def test_display_resolution_uses_backend_defaults() -> None:
    assert boot_debug.resolve_display('bochs', None) == 'sdl2'
    assert boot_debug.resolve_display('qemu', None) == 'graphical'
    assert boot_debug.resolve_display('qemu-gdb', None) == 'graphical'


def test_display_resolution_rejects_invalid_backend_combinations() -> None:
    with pytest.raises(boot_debug.StageError, match='unsupported display'):
        boot_debug.resolve_display('qemu', 'sdl2')
    with pytest.raises(boot_debug.StageError, match='unsupported display'):
        boot_debug.resolve_display('bochs', 'graphical')


def test_preflight_requires_only_selected_emulator(monkeypatch) -> None:
    def fake_which(tool: str) -> str | None:
        if tool == 'qemu-system-x86_64':
            return None
        return f'/usr/bin/{tool}'

    monkeypatch.setattr(boot_debug.shutil, 'which', fake_which)

    with pytest.raises(boot_debug.StageError, match='qemu-system-x86_64'):
        boot_debug.check_tools('qemu', need_emulator=True, need_build=False)
    boot_debug.check_tools('bochs', need_emulator=True, need_build=False)
    boot_debug.check_tools('qemu', need_emulator=False, need_build=False)


def test_preflight_requires_bochs_only_for_bochs_backend(monkeypatch) -> None:
    def fake_which(tool: str) -> str | None:
        if tool == 'bochs':
            return None
        return f'/usr/bin/{tool}'

    monkeypatch.setattr(boot_debug.shutil, 'which', fake_which)

    with pytest.raises(boot_debug.StageError, match='bochs'):
        boot_debug.check_tools('bochs', need_emulator=True, need_build=False)
    boot_debug.check_tools('qemu', need_emulator=True, need_build=False)


class FakeProcess:
    pid = 4242

    def __init__(self) -> None:
        self.returncode: int | None = None
        self.stdout = StringIO('')
        self.wait_calls = 0

    def poll(self) -> int | None:
        return self.returncode

    def wait(self, timeout: float | None = None) -> int:
        _ = timeout
        self.wait_calls += 1
        self.returncode = -15
        return self.returncode


def test_qemu_serial_marker_success_stops_process_group(tmp_path: Path, monkeypatch) -> None:
    serial_log = tmp_path / 'qemu.serial.log'
    process = FakeProcess()
    killed: list[tuple[int, int]] = []

    def fake_popen(*args, **kwargs):
        _ = args, kwargs
        serial_log.write_text('BIGOS_MM_SELF_TEST_PASSED\n', encoding='utf-8')
        return process

    monkeypatch.setattr(boot_debug.subprocess, 'Popen', fake_popen)
    monkeypatch.setattr(boot_debug.os, 'killpg', lambda pid, sig: killed.append((pid, sig)))
    monkeypatch.setattr(boot_debug.time, 'sleep', lambda seconds: None)

    boot_debug.launch_qemu_until_serial_marker(
        ['qemu-system-x86_64', '-serial', f'file:{serial_log}'],
        serial_log,
        'BIGOS_MM_SELF_TEST_PASSED',
        1.0,
    )

    assert killed == [(process.pid, boot_debug.signal.SIGTERM)]
    assert process.wait_calls == 1


def test_qemu_serial_marker_timeout_cleans_process_group(tmp_path: Path, monkeypatch) -> None:
    serial_log = tmp_path / 'qemu.serial.log'
    process = FakeProcess()
    killed: list[tuple[int, int]] = []
    monotonic_values = iter([0.0, 1.0])

    monkeypatch.setattr(boot_debug.subprocess, 'Popen', lambda *args, **kwargs: process)
    monkeypatch.setattr(boot_debug.os, 'killpg', lambda pid, sig: killed.append((pid, sig)))
    monkeypatch.setattr(boot_debug.time, 'monotonic', lambda: next(monotonic_values))

    with pytest.raises(boot_debug.StageError, match='qemu smoke'):
        boot_debug.launch_qemu_until_serial_marker(
            ['qemu-system-x86_64', '-serial', f'file:{serial_log}'],
            serial_log,
            'MISSING_MARKER',
            0.1,
        )

    assert killed == [(process.pid, boot_debug.signal.SIGTERM)]
    assert process.wait_calls == 1


def test_xmake_exposes_bochs_targets_and_boot_artifact_rules() -> None:
    xmake = (PROJECT_ROOT / 'xmake.lua').read_text(encoding='utf-8')

    assert 'target("bochs-sdl2")' not in xmake
    assert 'target("bochs")' in xmake
    assert 'target("qemu")' in xmake
    assert 'target("qemu-gdb")' in xmake
    assert 'target("qemu-headless")' not in xmake
    assert 'target("boot-artifacts")' in xmake
    assert 'build/bin/x86/boot' not in xmake
    assert 'path.join(boot_bindir, "mbr.bin")' in xmake and 'bytes > 512 bytes' in xmake
    assert 'path.join(boot_bindir, "dbr.bin")' in xmake and 'bytes > 512 bytes' in xmake
    assert 'path.join(boot_bindir, "exdbr.bin")' in xmake and 'bytes > 4096 bytes' in xmake
    assert 'path.join(boot_bindir, "boot.bin")' in xmake and 'bytes > 524288 bytes' in xmake
    assert '--skip-build' in xmake
    assert 'process.openv("python3", args)' in xmake
    assert 'option.get("arguments")' in xmake
    assert 'run_boot_debug("bochs", "build/test/bochs.serial.log",' in xmake
    assert 'run_boot_debug("qemu", "build/test/qemu.serial.log",' in xmake
    assert 'run_boot_debug("qemu-gdb", "build/test/qemu-gdb.serial.log",' in xmake
