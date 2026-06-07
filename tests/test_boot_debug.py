import importlib.util
import sys
from pathlib import Path

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


def test_generated_bochsrc_is_sanitized(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(image, bochsrc, None, None, None, [])

    contents = bochsrc.read_text(encoding='utf-8')
    assert f'path="{image}"' in contents
    assert 'boot: disk' in contents
    assert f'cpu: model={boot_debug.DEFAULT_CPU_MODEL}, count=1' in contents
    assert 'heads=16, spt=32' in contents
    assert 'cpuid:' not in contents
    assert 'win32' not in contents.lower()
    assert 'C:\\' not in contents


def test_generated_bochsrc_can_inject_sdl2_display(tmp_path: Path) -> None:
    image = write_bytes(tmp_path / 'os.raw', bytes(boot_debug.DEFAULT_IMAGE_SIZE))
    bochsrc = tmp_path / 'bochsrc.bxrc'

    boot_debug.render_bochsrc(image, bochsrc, None, None, None, ['display_library: sdl2'])

    contents = bochsrc.read_text(encoding='utf-8')
    assert 'display_library: sdl2' in contents


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

    assert commands == [['xmake', 'build', 'kernel'], ['xmake', 'build', 'boot-artifacts']]


def test_run_parser_rejects_smoke_shortcuts() -> None:
    parser = boot_debug.make_parser()

    for shortcut in ('--memory-self-test', '--user-program-smoke'):
        try:
            parser.parse_args(['run', shortcut])
        except SystemExit as error:
            assert error.code == 2
        else:
            raise AssertionError(f'{shortcut} should be rejected')


def test_run_parser_accepts_skip_build() -> None:
    parser = boot_debug.make_parser()

    args = parser.parse_args(['run', '--skip-build'])

    assert args.skip_build is True


def test_xmake_exposes_bochs_targets_and_boot_artifact_rules() -> None:
    xmake = (PROJECT_ROOT / 'xmake.lua').read_text(encoding='utf-8')

    assert 'target("bochs-sdl2")' in xmake
    assert 'target("bochs")' in xmake
    assert 'target("boot-artifacts")' in xmake
    assert 'build/bin/x86/boot' not in xmake
    assert 'path.join(boot_bindir, "mbr.bin")' in xmake and 'bytes > 512 bytes' in xmake
    assert 'path.join(boot_bindir, "dbr.bin")' in xmake and 'bytes > 512 bytes' in xmake
    assert 'path.join(boot_bindir, "exdbr.bin")' in xmake and 'bytes > 4096 bytes' in xmake
    assert 'path.join(boot_bindir, "boot.bin")' in xmake and 'bytes > 524288 bytes' in xmake
    assert '--skip-build' in xmake
    assert 'display_library: sdl2' in xmake
