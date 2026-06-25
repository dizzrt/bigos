#include <bigos/console_render.h>

#include <bigos/boot_handoff.h>
#include <bigos/device.h>
#include <bigos/glyph_font.h>
#include <bigos/io.h>
#include <bigos/memory.h>

namespace {
    struct FramebufferBackendState {
        bool ready;
        uint8_t *base;
        uint64_t byte_size;
        uint32_t width;
        uint32_t height;
        uint32_t pixels_per_scanline;
        uint32_t stride_bytes;
        uint16_t bytes_per_pixel;
        uint32_t pixel_format;
        uint16_t cell_width;
        uint16_t cell_height;
        uint8_t visible_columns;
        uint8_t visible_rows;
        bool cursor_visible;
        uint8_t cursor_x;
        uint8_t cursor_y;
    };

    FramebufferBackendState g_framebuffer = {};

    void vga_clear() noexcept {
        bigos::device::clear_video_text();
    }

    void vga_begin_viewport_redraw() noexcept {}

    char vga_cell_char(const bigos::terminal::ConsoleRenderCell &__cell) noexcept {
        if (__cell.role == bigos::terminal::ConsoleCellRole::Blank ||
            __cell.role == bigos::terminal::ConsoleCellRole::WideTrailing)
            return ' ';
        if (__cell.codepoint >= 0x20u && __cell.codepoint <= 0x7eu)
            return (char)__cell.codepoint;
        return '?';
    }

    uint8_t vga_color_byte(const bigos::terminal::ConsoleDisplayAttr &__attr) noexcept {
        return (uint8_t)(((__attr.background & 0x0fu) << 4) | (__attr.foreground & 0x0fu));
    }

    void vga_draw_cell(uint8_t __x, uint8_t __y, const bigos::terminal::ConsoleRenderCell &__cell) noexcept {
        bigos::device::fill_video_text_cell(__x, __y, vga_cell_char(__cell), vga_color_byte(__cell.attr));
    }

    void vga_end_viewport_redraw() noexcept {}

    void vga_set_cursor(uint8_t __x, uint8_t __y, bool __visible, const bigos::terminal::ConsoleRenderCell &) noexcept {
        if (__visible)
            bigos::device::set_video_text_cursor(__x, __y);
        else
            bigos::device::set_video_text_cursor(0, 0);
    }

    const bigos::terminal::ConsoleRenderBackend g_vga_backend = {
        "vga-text",
        bigos::terminal::CONSOLE_RENDER_VGA_WIDTH,
        bigos::terminal::CONSOLE_RENDER_VGA_HEIGHT,
        &vga_clear,
        &vga_begin_viewport_redraw,
        &vga_draw_cell,
        &vga_end_viewport_redraw,
        &vga_set_cursor,
    };

    uint32_t vga_component(uint8_t color, uint32_t shift) noexcept {
        return ((uint32_t)(color >> shift) & 1u) * 0xaau;
    }

    uint32_t palette_color(uint8_t color) noexcept {
        const uint8_t bright = (color & 0x08u) != 0 ? 0x55u : 0x00u;
        const uint8_t red = (uint8_t)(vga_component(color, 2) + bright);
        const uint8_t green = (uint8_t)(vga_component(color, 1) + bright);
        const uint8_t blue = (uint8_t)(vga_component(color, 0) + bright);
        return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    }

    bool framebuffer_cell_range_valid(uint8_t __x, uint8_t __y, uint8_t __span = 1) noexcept {
        if (!g_framebuffer.ready)
            return false;
        if (__x >= g_framebuffer.visible_columns || __y >= g_framebuffer.visible_rows)
            return false;
        if (__span == 0 || (uint32_t)__x + __span > g_framebuffer.visible_columns)
            return false;
        const uint64_t px = (uint64_t)__x * g_framebuffer.cell_width;
        const uint64_t py = (uint64_t)__y * g_framebuffer.cell_height;
        const uint64_t cell_pixel_width = (uint64_t)g_framebuffer.cell_width * __span;
        const uint64_t row_bytes = cell_pixel_width * g_framebuffer.bytes_per_pixel;
        if (px + cell_pixel_width > g_framebuffer.width || py + g_framebuffer.cell_height > g_framebuffer.height)
            return false;
        const uint64_t row_start = py * g_framebuffer.stride_bytes + px * g_framebuffer.bytes_per_pixel;
        const uint64_t last_row_start =
            (py + g_framebuffer.cell_height - 1) * g_framebuffer.stride_bytes + px * g_framebuffer.bytes_per_pixel;
        return row_start < g_framebuffer.byte_size && row_bytes <= g_framebuffer.byte_size - last_row_start;
    }

    void framebuffer_write_pixel(uint64_t __offset, uint32_t __rgb) noexcept {
        if (__offset + g_framebuffer.bytes_per_pixel > g_framebuffer.byte_size)
            return;
        volatile uint8_t *pixel = g_framebuffer.base + __offset;
        const uint8_t red = (uint8_t)(__rgb >> 16);
        const uint8_t green = (uint8_t)(__rgb >> 8);
        const uint8_t blue = (uint8_t)__rgb;
        if (g_framebuffer.pixel_format == BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_RGBX8888) {
            pixel[0] = red;
            pixel[1] = green;
            pixel[2] = blue;
            pixel[3] = 0;
        } else {
            pixel[0] = blue;
            pixel[1] = green;
            pixel[2] = red;
            pixel[3] = 0;
        }
    }

    void framebuffer_fill_cell(uint8_t __x, uint8_t __y, uint32_t __rgb, uint8_t __span = 1) noexcept {
        if (!framebuffer_cell_range_valid(__x, __y, __span))
            return;
        const uint64_t start_x = (uint64_t)__x * g_framebuffer.cell_width;
        const uint64_t start_y = (uint64_t)__y * g_framebuffer.cell_height;
        const uint64_t cell_pixel_width = (uint64_t)g_framebuffer.cell_width * __span;
        for (uint16_t row = 0; row < g_framebuffer.cell_height; ++row) {
            const uint64_t row_offset =
                (start_y + row) * g_framebuffer.stride_bytes + start_x * g_framebuffer.bytes_per_pixel;
            for (uint16_t col = 0; col < cell_pixel_width; ++col)
                framebuffer_write_pixel(row_offset + (uint64_t)col * g_framebuffer.bytes_per_pixel, __rgb);
        }
    }

    bool glyph_bit_set(const bigos::font::GlyphBitmap &__glyph, uint16_t __x, uint16_t __y) noexcept {
        if (__glyph.bitmap == nullptr || __x >= __glyph.glyph_width || __y >= __glyph.glyph_height)
            return false;
        const uint16_t bytes_per_row = (uint16_t)((__glyph.glyph_width + 7) / 8);
        const uint32_t offset = (uint32_t)__y * bytes_per_row + __x / 8;
        if (offset >= __glyph.bitmap_size)
            return false;
        const uint8_t mask = (uint8_t)(0x80u >> (__x % 8));
        return (__glyph.bitmap[offset] & mask) != 0;
    }

    bool lookup_render_glyph(uint32_t __codepoint, bigos::font::GlyphBitmap *__glyph) noexcept {
        if (__codepoint < 0x20u || __codepoint == 0x7fu)
            return false;
        if (bigos::font::lookup_glyph(__codepoint, __glyph) == bigos::font::GlyphLookupStatus::Found)
            return true;
        if (bigos::font::lookup_glyph(0xfffdu, __glyph) == bigos::font::GlyphLookupStatus::Found)
            return true;
        return bigos::font::lookup_glyph('?', __glyph) == bigos::font::GlyphLookupStatus::Found;
    }

    void framebuffer_draw_cell(
        uint8_t __x, uint8_t __y, const bigos::terminal::ConsoleRenderCell &__cell, bool __cursor) noexcept {
        if (__cell.role == bigos::terminal::ConsoleCellRole::WideTrailing)
            return;
        const uint8_t span = __cell.role == bigos::terminal::ConsoleCellRole::WideLeading ? 2 : 1;
        const uint32_t fg = palette_color((uint8_t)(__cell.attr.foreground & 0x0fu));
        const uint32_t bg = palette_color((uint8_t)(__cell.attr.background & 0x0fu));
        const uint32_t draw_fg = __cursor ? bg : fg;
        const uint32_t draw_bg = __cursor ? fg : bg;
        framebuffer_fill_cell(__x, __y, draw_bg, span);
        if (__cell.role == bigos::terminal::ConsoleCellRole::Blank)
            return;

        bigos::font::GlyphBitmap glyph = {};
        if (!lookup_render_glyph(__cell.codepoint, &glyph))
            return;
        const uint16_t draw_width =
            glyph.glyph_width < g_framebuffer.cell_width * span ? glyph.glyph_width : g_framebuffer.cell_width * span;
        const uint16_t draw_height =
            glyph.glyph_height < g_framebuffer.cell_height ? glyph.glyph_height : g_framebuffer.cell_height;
        const uint64_t start_x = (uint64_t)__x * g_framebuffer.cell_width;
        const uint64_t start_y = (uint64_t)__y * g_framebuffer.cell_height;
        for (uint16_t row = 0; row < draw_height; ++row) {
            const uint64_t row_offset =
                (start_y + row) * g_framebuffer.stride_bytes + start_x * g_framebuffer.bytes_per_pixel;
            for (uint16_t col = 0; col < draw_width; ++col) {
                if (glyph_bit_set(glyph, col, row))
                    framebuffer_write_pixel(row_offset + (uint64_t)col * g_framebuffer.bytes_per_pixel, draw_fg);
            }
        }
    }

    void framebuffer_clear() noexcept {
        if (!g_framebuffer.ready)
            return;
        const uint32_t bg = palette_color(0);
        for (uint32_t y = 0; y < g_framebuffer.height; ++y) {
            const uint64_t row_offset = (uint64_t)y * g_framebuffer.stride_bytes;
            for (uint32_t x = 0; x < g_framebuffer.pixels_per_scanline; ++x)
                framebuffer_write_pixel(row_offset + (uint64_t)x * g_framebuffer.bytes_per_pixel, bg);
        }
    }

    void framebuffer_begin_viewport_redraw() noexcept {
        g_framebuffer.cursor_visible = false;
    }

    void framebuffer_draw_cell_backend(
        uint8_t __x, uint8_t __y, const bigos::terminal::ConsoleRenderCell &__cell) noexcept {
        framebuffer_draw_cell(__x, __y, __cell, false);
    }

    void framebuffer_end_viewport_redraw() noexcept {}

    void framebuffer_set_cursor(
        uint8_t __x, uint8_t __y, bool __visible, const bigos::terminal::ConsoleRenderCell &__cell) noexcept {
        g_framebuffer.cursor_x = __x;
        g_framebuffer.cursor_y = __y;
        g_framebuffer.cursor_visible = __visible;
        if (!__visible)
            return;
        framebuffer_draw_cell(__x, __y, __cell, true);
    }

    bigos::terminal::ConsoleRenderBackend g_framebuffer_backend = {
        "framebuffer-text",
        0,
        0,
        &framebuffer_clear,
        &framebuffer_begin_viewport_redraw,
        &framebuffer_draw_cell_backend,
        &framebuffer_end_viewport_redraw,
        &framebuffer_set_cursor,
    };

    const bigos::terminal::ConsoleRenderBackend *g_selected_backend = &g_vga_backend;

    bool choose_cell_metrics(uint16_t &__cell_width, uint16_t &__cell_height) noexcept {
        bigos::font::GlyphBitmap glyph = {};
        if (bigos::font::lookup_glyph(' ', &glyph) != bigos::font::GlyphLookupStatus::Found &&
            bigos::font::lookup_glyph('?', &glyph) != bigos::font::GlyphLookupStatus::Found &&
            bigos::font::lookup_glyph('A', &glyph) != bigos::font::GlyphLookupStatus::Found)
            return false;
        if (glyph.cell_width == 0 || glyph.cell_height == 0)
            return false;
        __cell_width = glyph.cell_width;
        __cell_height = glyph.cell_height;
        return true;
    }

    bool checked_mul_u64(uint64_t __a, uint64_t __b, uint64_t &__out) noexcept {
        if (__a != 0 && __b > ~0ull / __a)
            return false;
        __out = __a * __b;
        return true;
    }

    bool checked_add_u64(uint64_t __a, uint64_t __b, uint64_t &__out) noexcept {
        if (__b > ~0ull - __a)
            return false;
        __out = __a + __b;
        return true;
    }

    uint8_t clamp_framebuffer_grid_dimension(uint64_t __value, uint8_t __max) noexcept {
        return __value > __max ? __max : (uint8_t)__value;
    }

    bool framebuffer_pixel_rect_valid(uint64_t __x, uint64_t __y, uint64_t __width, uint64_t __height,
        uint64_t __stride_bytes, uint64_t __bytes_per_pixel, uint64_t __byte_size) noexcept {
        if (__width == 0 || __height == 0 || __bytes_per_pixel == 0)
            return false;
        uint64_t row_bytes = 0;
        if (!checked_mul_u64(__width, __bytes_per_pixel, row_bytes))
            return false;
        uint64_t x_bytes = 0;
        if (!checked_mul_u64(__x, __bytes_per_pixel, x_bytes))
            return false;
        uint64_t start_y_offset = 0;
        if (!checked_mul_u64(__y, __stride_bytes, start_y_offset))
            return false;
        uint64_t row_start = 0;
        if (!checked_add_u64(start_y_offset, x_bytes, row_start))
            return false;
        uint64_t last_y = 0;
        if (!checked_add_u64(__y, __height - 1, last_y))
            return false;
        uint64_t last_y_offset = 0;
        if (!checked_mul_u64(last_y, __stride_bytes, last_y_offset))
            return false;
        uint64_t last_row_start = 0;
        if (!checked_add_u64(last_y_offset, x_bytes, last_row_start))
            return false;
        return row_start < __byte_size && last_row_start < __byte_size && row_bytes <= __byte_size - last_row_start;
    }

    bool probe_framebuffer_backend() noexcept {
        g_framebuffer = {};
        const auto &fb = bigos::boot::early_framebuffer();
        if (fb.status != BootOptionalSectionStatus::Valid || !bigos::font::glyph_lookup_available())
            return false;
        const BootFramebufferMetadata &metadata = fb.metadata;
        if (!bigos_boot_framebuffer_metadata_valid(&metadata))
            return false;
        if (metadata.pixel_format != BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_BGRX8888 &&
            metadata.pixel_format != BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_RGBX8888)
            return false;
        if (metadata.bits_per_pixel != 32 || metadata.bytes_per_pixel != 4)
            return false;

        uint16_t cell_width = 0;
        uint16_t cell_height = 0;
        if (!choose_cell_metrics(cell_width, cell_height))
            return false;
        const uint64_t raw_columns = metadata.width / cell_width;
        const uint64_t raw_rows = metadata.height / cell_height;
        if (raw_columns < bigos::terminal::CONSOLE_RENDER_MIN_WIDTH ||
            raw_rows < bigos::terminal::CONSOLE_RENDER_MIN_HEIGHT)
            return false;
        const uint8_t visible_columns =
            clamp_framebuffer_grid_dimension(raw_columns, bigos::terminal::CONSOLE_RENDER_MAX_WIDTH);
        const uint8_t visible_rows =
            clamp_framebuffer_grid_dimension(raw_rows, bigos::terminal::CONSOLE_RENDER_MAX_HEIGHT);
        uint64_t stride_bytes = 0;
        if (!checked_mul_u64(metadata.pixels_per_scanline, metadata.bytes_per_pixel, stride_bytes))
            return false;
        if (stride_bytes > 0xffffffffull)
            return false;
        uint64_t min_size = 0;
        if (!checked_mul_u64(metadata.height, stride_bytes, min_size) || min_size > metadata.byte_size)
            return false;
        uint64_t grid_width = 0;
        uint64_t grid_height = 0;
        if (!checked_mul_u64(visible_columns, cell_width, grid_width) ||
            !checked_mul_u64(visible_rows, cell_height, grid_height))
            return false;
        if (grid_width > metadata.width || grid_height > metadata.height)
            return false;
        if (!framebuffer_pixel_rect_valid(
                0, 0, grid_width, grid_height, stride_bytes, metadata.bytes_per_pixel, metadata.byte_size))
            return false;
        if (!framebuffer_pixel_rect_valid(0, 0, metadata.pixels_per_scanline, metadata.height, stride_bytes,
                metadata.bytes_per_pixel, metadata.byte_size))
            return false;
        const auto policy = (metadata.attributes & BIGOS_BOOT_FRAMEBUFFER_ATTR_WRITE_COMBINE) != 0
                                ? bigos::mm::DeviceMmioCachePolicy::WriteCombining
                                : bigos::mm::DeviceMmioCachePolicy::Uncached;
        const bigos::mm::DeviceMmioMapping mapping =
            bigos::mm::map_device_mmio(metadata.physical_base, metadata.byte_size, policy);
        if (!mapping.valid || mapping.vaddr == nullptr || mapping.length < metadata.byte_size ||
            mapping.length < min_size)
            return false;

        g_framebuffer.ready = true;
        g_framebuffer.base = (uint8_t *)mapping.vaddr;
        g_framebuffer.byte_size = mapping.length;
        g_framebuffer.width = metadata.width;
        g_framebuffer.height = metadata.height;
        g_framebuffer.pixels_per_scanline = metadata.pixels_per_scanline;
        g_framebuffer.stride_bytes = (uint32_t)stride_bytes;
        g_framebuffer.bytes_per_pixel = metadata.bytes_per_pixel;
        g_framebuffer.pixel_format = metadata.pixel_format;
        g_framebuffer.cell_width = cell_width;
        g_framebuffer.cell_height = cell_height;
        g_framebuffer.visible_columns = visible_columns;
        g_framebuffer.visible_rows = visible_rows;
        return true;
    }
}   // namespace

namespace bigos::terminal {
    void init_console_render_backend() noexcept {
        g_selected_backend = &g_vga_backend;
        if (probe_framebuffer_backend()) {
            g_framebuffer_backend.visible_columns = g_framebuffer.visible_columns;
            g_framebuffer_backend.visible_rows = g_framebuffer.visible_rows;
            g_framebuffer_backend.clear();
            g_selected_backend = &g_framebuffer_backend;
            bigos::serial_puts("BIGOS_CONSOLE_RENDER backend=framebuffer-text\n");
        } else {
            bigos::serial_puts("BIGOS_CONSOLE_RENDER backend=vga-text\n");
        }
    }

    const ConsoleRenderBackend &console_render_backend() noexcept {
        return *g_selected_backend;
    }
}   // namespace bigos::terminal
