from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (PROJECT_ROOT / path).read_text()


def test_bootinfo_framebuffer_and_font_sections_are_versioned_and_checked() -> None:
    boot_info = read_source('include/arch/x86/boot/boot_info.h')

    assert '#define BIGOS_BOOT_SECTION_TYPE_FRAMEBUFFER_METADATA 5u' in boot_info
    assert '#define BIGOS_BOOT_SECTION_TYPE_FONT_ASSET_METADATA  6u' in boot_info
    assert 'struct BootFramebufferMetadata' in boot_info
    assert 'struct BootFontAssetMetadata' in boot_info
    assert 'struct BootFontAssetHeader' in boot_info
    assert 'struct BootFontAssetRangeRecord' in boot_info
    assert 'struct BootFontAssetGlyphRecord' in boot_info
    assert '#define BIGOS_BOOT_FONT_FORMAT_GLYPH_LOOKUP_V1 2u' in boot_info
    assert 'enum class BootOptionalSectionStatus' in boot_info
    assert 'bigos_boot_info_v2_framebuffer_metadata' in boot_info
    assert 'bigos_boot_info_v2_font_asset_metadata' in boot_info
    assert 'static_assert(sizeof(BootFramebufferMetadata) == 40)' in boot_info
    assert 'static_assert(sizeof(BootFontAssetMetadata) == 40)' in boot_info
    assert 'static_assert(sizeof(BootFontAssetHeader) == BIGOS_BOOT_FONT_ASSET_HEADER_SIZE)' in boot_info
    assert 'static_assert(sizeof(BootFontAssetRangeRecord) == BIGOS_BOOT_FONT_ASSET_RANGE_SIZE)' in boot_info
    assert 'static_assert(sizeof(BootFontAssetGlyphRecord) == BIGOS_BOOT_FONT_ASSET_GLYPH_SIZE)' in boot_info
    assert 'BIGOS_BOOT_INFO_V2_MAGIC' in boot_info
    assert 'static_assert(sizeof(BootInfoHeader) == 24)' in boot_info
    assert 'static_assert(sizeof(BootInfo) == BIGOS_BOOT_INFO_SIZE)' in boot_info


def test_uefi_loader_collects_gop_and_font_without_claiming_console_ready() -> None:
    loader = read_source('kernel/arch/x86/uefi/loader.cc')
    uefi = read_source('kernel/arch/x86/uefi/uefi.h')
    boot_debug = read_source('tools/bigosdev/core.py')

    assert 'EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID' in uefi
    assert 'struct EFI_GRAPHICS_OUTPUT_PROTOCOL' in uefi
    assert 'prepare_framebuffer_handoff()' in loader
    assert 'locate_protocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID' in loader
    assert 'normalize_gop_pixel_format' in loader
    assert 'BIGOS_UEFI_FRAMEBUFFER' in loader
    assert 'BIGOS_UEFI_FONT' in loader
    assert 'u"\\\\boot\\\\fonts\\\\unifont.bin"' in loader
    assert 'BIGOS_BOOT_FONT_FORMAT_GLYPH_LOOKUP_V1' in loader
    assert 'header->payload_size != file_size' in loader
    assert 'sizeof(BootFontAssetRangeRecord)' in loader
    assert 'sizeof(BootFontAssetGlyphRecord)' in loader
    assert 'BIGOS_BOOT_SECTION_TYPE_FRAMEBUFFER_METADATA' in loader
    assert 'BIGOS_BOOT_SECTION_TYPE_FONT_ASSET_METADATA' in loader
    assert "FONT_SOURCE = PROJECT_ROOT / 'assets' / 'fonts' / 'unifont_all-17.0.04.hex'" in boot_debug
    assert "FONT_ASSET = BUILD_DIR / 'assets' / 'fonts' / 'unifont.bin'" in boot_debug
    assert "FONT_ASSET_PATH = '/boot/fonts/unifont.bin'" in boot_debug
    assert 'generate_boot_font_asset(FONT_SOURCE, FONT_ASSET)' in boot_debug
    assert 'mtools_copy_in(image_path, artifacts.font_asset, FONT_ASSET_PATH)' in boot_debug


def test_kernel_consumes_optional_metadata_without_framebuffer_direct_map_alias() -> None:
    handoff = read_source('kernel/mm/boot_handoff.cc')
    buddy = read_source('kernel/mm/buddy.cc')
    vmem = read_source('kernel/mm/vmem.cc')
    memory = read_source('include/bigos/memory.h')
    render = read_source('kernel/core/terminal/console_render.cc')

    assert 'init_early_handoff_views' in handoff
    assert 'BootOptionalSectionStatus::Valid' in handoff
    assert 'consume_region_excluding_framebuffer' in buddy
    assert 'bigos_boot_info_v2_framebuffer_metadata(header)' in buddy
    assert 'init_direct_map_from_region_excluding_framebuffer' in vmem
    assert 'bigos_boot_info_v2_framebuffer_metadata(__header)' in vmem
    assert 'DeviceMmioMapping map_device_mmio' in memory
    assert 'KDEVICE_MMIO_BASE' in vmem
    assert 'DEVICE_UNCACHED' in memory
    assert 'page_attr::DEVICE_UNCACHED' in vmem
    assert 'return (void *)(KDIRECT_BASE + __phys);' in vmem
    assert 'bigos::boot::early_framebuffer()' in render
    assert 'bigos::mm::map_device_mmio(metadata.physical_base, metadata.byte_size, policy)' in render
    assert 'phys_to_direct' not in render


def test_framebuffer_console_backend_reports_dynamic_grid_and_renders_glyph_cells() -> None:
    render_h = read_source('include/bigos/console_render.h')
    render = read_source('kernel/core/terminal/console_render.cc')
    console = read_source('kernel/core/terminal/console.cc')

    assert 'struct ConsoleRenderBackend' in render_h
    assert 'struct ConsoleDisplayAttr' in render_h
    assert 'ConsoleDisplayAttr attr;' in render_h
    assert 'CONSOLE_RENDER_VGA_WIDTH = 80' in render_h
    assert 'CONSOLE_RENDER_VGA_HEIGHT = 25' in render_h
    assert 'CONSOLE_RENDER_MAX_WIDTH = 240' in render_h
    assert 'CONSOLE_RENDER_MAX_HEIGHT = 80' in render_h
    assert 'uint8_t visible_columns;' in render_h
    assert 'uint8_t visible_rows;' in render_h
    assert 'init_console_render_backend();' in console
    assert 'g_console.visible_columns = backend.visible_columns;' in console
    assert 'ConsoleRenderCell lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_RENDER_MAX_WIDTH];' in console
    assert 'probe_framebuffer_backend()' in render
    assert 'bigos_boot_framebuffer_metadata_valid(&metadata)' in render
    assert 'metadata.pixel_format != BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_BGRX8888' in render
    assert 'metadata.bits_per_pixel != 32 || metadata.bytes_per_pixel != 4' in render
    assert 'const uint64_t raw_columns = metadata.width / cell_width;' in render
    assert 'const uint64_t raw_rows = metadata.height / cell_height;' in render
    assert 'CONSOLE_RENDER_MIN_WIDTH' in render
    assert 'CONSOLE_RENDER_MAX_WIDTH' in render
    assert 'clamp_framebuffer_grid_dimension(raw_columns' in render
    assert 'g_framebuffer_backend.visible_columns = g_framebuffer.visible_columns;' in render
    assert 'grid_width > metadata.width || grid_height > metadata.height' in render
    assert 'metadata.height, stride_bytes, min_size' in render
    assert 'framebuffer_pixel_rect_valid' in render
    assert 'lookup_render_glyph' in render
    assert 'bigos::font::lookup_glyph(__codepoint, __glyph)' in render
    assert 'bigos::font::lookup_glyph(0xfffdu, __glyph)' in render
    assert "bigos::font::lookup_glyph('?', __glyph)" in render
    assert 'ConsoleCellRole::WideTrailing' in render
    assert 'const uint8_t span = __cell.role == bigos::terminal::ConsoleCellRole::WideLeading ? 2 : 1;' in render
    assert 'framebuffer_fill_cell(__x, __y, draw_bg, span);' in render
    assert '__cell.attr.foreground' in render
    assert '__cell.attr.background' in render
    assert 'vga_color_byte' in render
    assert 'glyph_bit_set' in render
    assert 'framebuffer_set_cursor' in render
    assert 'framebuffer_draw_cell(__x, __y, __cell, true);' in render
    assert 'BIGOS_CONSOLE_RENDER backend=framebuffer-text' in render
    assert 'BIGOS_CONSOLE_RENDER backend=vga-text' in render


def test_framebuffer_full_clear_stays_within_validated_mapping_bounds() -> None:
    render = read_source('kernel/core/terminal/console_render.cc')

    assert 'void framebuffer_clear() noexcept' in render
    assert 'for (uint32_t y = 0; y < g_framebuffer.height; ++y)' in render
    assert 'for (uint32_t x = 0; x < g_framebuffer.pixels_per_scanline; ++x)' in render
    assert 'framebuffer_write_pixel(row_offset + (uint64_t)x * g_framebuffer.bytes_per_pixel, bg);' in render
    assert '__offset + g_framebuffer.bytes_per_pixel > g_framebuffer.byte_size' in render
    assert 'mapping.length < metadata.byte_size' in render
    assert 'mapping.length < min_size' in render
    assert 'framebuffer_pixel_rect_valid(0, 0, metadata.pixels_per_scanline, metadata.height' in render
