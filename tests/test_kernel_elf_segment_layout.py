import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def section_body(link_script: str, section: str) -> str:
    match = re.search(rf'\n    {re.escape(section)}(?:\s|:)', link_script)
    assert match is not None
    start = match.start()
    end = link_script.index('\n\n', start)
    return link_script[start:end]


def test_linker_script_uses_split_kernel_load_segments() -> None:
    link = read_source('link.lds')

    assert 'kernel PT_LOAD FLAGS(7)' not in link
    assert 'FLAGS(7)' not in link
    assert 'text PT_LOAD FLAGS(5);' in link
    assert 'rodata PT_LOAD FLAGS(4);' in link
    assert 'data PT_LOAD FLAGS(6);' in link


def test_linker_script_maps_sections_to_expected_segments() -> None:
    link = read_source('link.lds')

    for section in ('.bigos', '.init', '.text', '.fini'):
        assert section_body(link, section).rstrip().endswith('} : text')

    for section in ('.rodata', '.rodata1', '.eh_frame_hdr', '.eh_frame'):
        assert section_body(link, section).rstrip().endswith('} : rodata')

    for section in ('.ctors', '.dtors', '.data', '.4k_area', '.bss'):
        assert section_body(link, section).rstrip().endswith('} : data')

    text_end = link.index('.fini :')
    rodata_start = link.index('.rodata :')
    data_start = link.index('.ctors :')
    assert '. = ALIGN(0x1000);' in link[text_end:rodata_start]
    assert '. = ALIGN(0x1000);' in link[rodata_start:data_start]


def test_built_kernel_elf_has_no_writable_executable_load_segment() -> None:
    readelf = shutil.which('x86_64-elf-readelf')
    if readelf is None:
        pytest.skip('x86_64-elf-readelf is unavailable')

    kernel = ROOT / 'build/kernel'
    if not kernel.exists():
        pytest.skip('build/kernel is unavailable; run xmake first')

    output = subprocess.check_output([readelf, '-lW', str(kernel)], text=True)
    load_lines = [line for line in output.splitlines() if line.strip().startswith('LOAD')]
    load_flags = {''.join(line.split()[6:-1]) for line in load_lines}

    assert 'RWE' not in load_flags
    assert {'RE', 'R', 'RW'} <= load_flags
