from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD_ACTIVE_PATHS = (
    'src/' + 'arch',
    'src/' + 'kernel',
    'src/' + 'drivers',
    'src/' + 'mm',
    'src/' + 'runtime',
)


def iter_text_files(root: Path):
    suffixes = {'.md', '.py', '.yaml', '.yml', '.lua', '.json', '.txt'}
    for path in root.rglob('*'):
        if path.is_file() and path.suffix in suffixes:
            yield path


def test_kernel_source_root_layout_is_migrated() -> None:
    assert not (ROOT / 'src').exists()
    assert (ROOT / 'kernel/arch/x86/boot').is_dir()
    assert (ROOT / 'kernel/core').is_dir()
    assert (ROOT / 'kernel/drivers').is_dir()
    assert (ROOT / 'kernel/mm').is_dir()
    assert (ROOT / 'kernel/runtime').is_dir()


def test_kernel_runtime_makefile_is_not_active_entry() -> None:
    assert not (ROOT / 'kernel/runtime/Makefile').exists()
    assert not (ROOT / 'kernel/arch/x86/boot/Makefile').exists()
    assert not (ROOT / 'Makefile').exists()


def test_targeted_old_active_paths_are_absent_outside_current_change() -> None:
    scanned_roots = [
        ROOT / 'README.md',
        ROOT / 'README-zh.md',
        ROOT / 'AGENTS.md',
        ROOT / 'docs',
        ROOT / 'tools',
        ROOT / 'tests',
        ROOT / 'openspec/config.yaml',
        ROOT / 'openspec/specs',
        ROOT / 'openspec/changes/archive',
        ROOT / 'xmake.lua',
        ROOT / 'xmake',
    ]

    files: list[Path] = []
    for root in scanned_roots:
        if root.is_file():
            files.append(root)
        else:
            files.extend(iter_text_files(root))

    for path in files:
        text = path.read_text(encoding='utf-8')
        for old_path in OLD_ACTIVE_PATHS:
            assert old_path not in text, path
