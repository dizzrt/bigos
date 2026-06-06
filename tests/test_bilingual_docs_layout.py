from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def markdown_paths(root: Path) -> set[str]:
    return {path.relative_to(root).as_posix() for path in root.rglob('*.md')}


def test_bilingual_docs_trees_are_isomorphic() -> None:
    en_root = ROOT / 'docs/en'
    zh_root = ROOT / 'docs/zh'

    assert en_root.is_dir()
    assert zh_root.is_dir()
    assert markdown_paths(en_root) == markdown_paths(zh_root)


def test_removed_top_level_doc_roots_are_absent() -> None:
    assert not (ROOT / 'docs' / 'arch').exists()
    assert not (ROOT / 'docs' / 'ktl').exists()


def test_active_references_use_canonical_language_roots() -> None:
    scanned_roots = [
        ROOT / 'README.md',
        ROOT / 'README-zh.md',
        ROOT / 'AGENTS.md',
        ROOT / 'docs',
        ROOT / 'tests',
        ROOT / 'openspec/specs',
    ]

    files: list[Path] = []
    for root in scanned_roots:
        if root.is_file():
            files.append(root)
        else:
            files.extend(path for path in root.rglob('*') if path.suffix in {'.md', '.py'})

    for path in files:
        text = path.read_text(encoding='utf-8')
        assert 'docs/' + 'arch' not in text, path
        assert 'docs/' + 'ktl' not in text, path


def test_repository_doc_references_are_portable() -> None:
    files = [
        ROOT / 'README.md',
        ROOT / 'README-zh.md',
        ROOT / 'AGENTS.md',
    ]
    files.extend(path for path in (ROOT / 'docs').rglob('*.md'))
    files.extend(path for path in (ROOT / 'tests').rglob('*.py'))
    files.extend(path for path in (ROOT / 'openspec/specs').rglob('*.md'))
    files.extend(path for path in (ROOT / 'openspec/changes/archive').rglob('*.md'))

    absolute_markers = ('/Users/', 'file:///', 'C:\\')
    for path in files:
        text = path.read_text(encoding='utf-8')
        for line in text.splitlines():
            if 'docs/' not in line:
                continue
            for marker in absolute_markers:
                assert marker not in line, path
