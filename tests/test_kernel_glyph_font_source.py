from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (PROJECT_ROOT / path).read_text()


def test_kernel_glyph_lookup_exposes_bounded_readonly_api() -> None:
    header = read_source('include/bigos/glyph_font.h')
    source = read_source('kernel/core/terminal/glyph_font.cc')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'enum class GlyphLookupStatus' in header
    assert 'Unavailable' in header
    assert 'InvalidAsset' in header
    assert 'NotFound' in header
    assert 'Found' in header
    assert 'struct GlyphBitmap' in header
    assert 'init_kernel_glyph_lookup()' in header
    assert 'lookup_glyph(uint32_t __codepoint, GlyphBitmap *__out)' in header

    assert 'bigos::boot::early_font_asset()' in source
    assert 'bigos::mm::is_direct_mapped_phys' in source
    assert 'BIGOS_BOOT_FONT_FORMAT_GLYPH_LOOKUP_V1' in source
    assert 'validate_header' in source
    assert 'validate_ranges_and_glyphs' in source
    assert 'find_range' in source
    assert 'find_glyph' in source
    assert 'GlyphLookupStatus::NotFound' in source
    assert 'BIGOS_FONT_LOOKUP ready' in source
    assert 'bigos::font::init_kernel_glyph_lookup();' in kernel


def test_kernel_glyph_lookup_stays_out_of_renderer_and_allocator_paths() -> None:
    source = read_source('kernel/core/terminal/glyph_font.cc')

    forbidden_tokens = [
        'kmalloc',
        'alloc_kernel_pages',
        'free(',
        'open(',
        'read(',
        'write(',
        'map_device_mmio',
        'framebuffer',
        'RuntimeServices',
        'UTF-8',
    ]
    for token in forbidden_tokens:
        assert token not in source
