#include <bigos/console.h>
#include <bigos/console_render.h>
#include <bigos/glyph_font.h>

namespace bigos::terminal {
    namespace {
        constexpr uint32_t CONSOLE_SCROLLBACK_LINES = 256;
        constexpr uint8_t CONSOLE_COLOR = 0x0f;
        constexpr uint32_t REPLACEMENT_CODEPOINT = 0xfffd;
        constexpr uint32_t QUESTION_CODEPOINT = '?';
        constexpr uint32_t SPACE_CODEPOINT = ' ';
        constexpr uint8_t TAB_STOP_COLUMNS = 4;

        struct Utf8Decoder {
            uint32_t codepoint;
            uint32_t min_codepoint;
            uint8_t remaining;
        };

        struct ConsoleState {
            ConsoleRenderCell lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_RENDER_MAX_WIDTH];
            uint64_t oldest_line;
            uint64_t current_line;
            uint64_t viewport_top;
            uint8_t visible_columns;
            uint8_t visible_rows;
            uint8_t cursor_x;
            Utf8Decoder utf8;
            bool initialized;
        };

        bool g_console_ready = false;
        ConsoleState g_console;

        ConsoleRenderCell blank_cell() noexcept {
            return {SPACE_CODEPOINT, ConsoleCellRole::Blank, CONSOLE_COLOR};
        }

        ConsoleRenderCell single_cell(uint32_t codepoint, ConsoleCellRole role = ConsoleCellRole::Single) noexcept {
            return {codepoint, role, CONSOLE_COLOR};
        }

        uint32_t line_slot(uint64_t line) noexcept {
            return (uint32_t)(line % CONSOLE_SCROLLBACK_LINES);
        }

        uint8_t visible_columns() noexcept {
            return g_console.visible_columns >= CONSOLE_RENDER_MIN_WIDTH ? g_console.visible_columns
                                                                         : CONSOLE_RENDER_VGA_WIDTH;
        }

        uint8_t visible_rows() noexcept {
            return g_console.visible_rows >= CONSOLE_RENDER_MIN_HEIGHT ? g_console.visible_rows
                                                                       : CONSOLE_RENDER_VGA_HEIGHT;
        }

        void clear_line(uint64_t line) noexcept {
            ConsoleRenderCell *cells = g_console.lines[line_slot(line)];
            const uint8_t columns = visible_columns();
            for (uint32_t x = 0; x < columns; ++x) {
                cells[x] = blank_cell();
            }
        }

        uint64_t bottom_viewport_top() noexcept {
            const uint8_t rows = visible_rows();
            const uint64_t natural_top = g_console.current_line + 1 > rows
                ? g_console.current_line + 1 - rows
                : 0;
            return natural_top < g_console.oldest_line ? g_console.oldest_line : natural_top;
        }

        bool viewport_at_bottom() noexcept {
            return g_console.viewport_top == bottom_viewport_top();
        }

        void clamp_viewport() noexcept {
            const uint64_t bottom = bottom_viewport_top();
            if (g_console.viewport_top < g_console.oldest_line)
                g_console.viewport_top = g_console.oldest_line;
            if (g_console.viewport_top > bottom)
                g_console.viewport_top = bottom;
        }

        void render_viewport() noexcept {
            if (!g_console.initialized)
                return;

            const ConsoleRenderBackend &backend = console_render_backend();
            const ConsoleRenderCell blank = blank_cell();
            backend.begin_viewport_redraw();
            clamp_viewport();
            const uint8_t columns = visible_columns();
            const uint8_t rows = visible_rows();
            for (uint32_t y = 0; y < rows; ++y) {
                const uint64_t line = g_console.viewport_top + y;
                for (uint32_t x = 0; x < columns; ++x) {
                    if (line >= g_console.oldest_line && line <= g_console.current_line) {
                        const ConsoleRenderCell &cell = g_console.lines[line_slot(line)][x];
                        backend.draw_cell((uint8_t)x, (uint8_t)y, cell);
                    } else {
                        backend.draw_cell((uint8_t)x, (uint8_t)y, blank);
                    }
                }
            }

            backend.end_viewport_redraw();
            if (g_console.current_line >= g_console.viewport_top) {
                const uint64_t cursor_screen_line = g_console.current_line - g_console.viewport_top;
                if (cursor_screen_line < rows && g_console.cursor_x < columns) {
                    const ConsoleRenderCell &cell =
                        g_console.lines[line_slot(g_console.current_line)][g_console.cursor_x];
                    backend.set_cursor(g_console.cursor_x, (uint8_t)cursor_screen_line, true, cell);
                    return;
                }
            }
            backend.set_cursor(0, 0, false, blank);
        }

        void reset_console_state() noexcept {
            const ConsoleRenderBackend &backend = console_render_backend();
            g_console.visible_columns = backend.visible_columns;
            g_console.visible_rows = backend.visible_rows;
            if (g_console.visible_columns < CONSOLE_RENDER_MIN_WIDTH ||
                g_console.visible_columns > CONSOLE_RENDER_MAX_WIDTH)
                g_console.visible_columns = CONSOLE_RENDER_VGA_WIDTH;
            if (g_console.visible_rows < CONSOLE_RENDER_MIN_HEIGHT ||
                g_console.visible_rows > CONSOLE_RENDER_MAX_HEIGHT)
                g_console.visible_rows = CONSOLE_RENDER_VGA_HEIGHT;
            g_console.oldest_line = 0;
            g_console.current_line = 0;
            g_console.viewport_top = 0;
            g_console.cursor_x = 0;
            g_console.utf8 = {};
            g_console.initialized = true;
            for (uint32_t line = 0; line < CONSOLE_SCROLLBACK_LINES; ++line)
                clear_line(line);
        }

        void advance_line() noexcept {
            ++g_console.current_line;
            if (g_console.current_line - g_console.oldest_line >= CONSOLE_SCROLLBACK_LINES)
                g_console.oldest_line = g_console.current_line + 1 - CONSOLE_SCROLLBACK_LINES;
            clear_line(g_console.current_line);
            g_console.cursor_x = 0;
        }

        bool is_utf8_continuation(uint8_t byte) noexcept {
            return (byte & 0xc0u) == 0x80u;
        }

        bool valid_unicode_scalar(uint32_t codepoint) noexcept {
            return codepoint <= 0x10ffffu && !(codepoint >= 0xd800u && codepoint <= 0xdfffu);
        }

        void reset_decoder() noexcept {
            g_console.utf8 = {};
        }

        uint8_t codepoint_cell_width(uint32_t codepoint, uint32_t &render_codepoint, ConsoleCellRole &role) noexcept {
            render_codepoint = codepoint;
            role = ConsoleCellRole::Single;
            if (codepoint >= 0x20u && codepoint <= 0x7eu)
                return 1;

            bigos::font::GlyphBitmap glyph = {};
            if (bigos::font::lookup_glyph(codepoint, &glyph) == bigos::font::GlyphLookupStatus::Found) {
                if (glyph.width_class == bigos::font::GlyphWidthClass::Full)
                    return 2;
                return 1;
            }

            render_codepoint = REPLACEMENT_CODEPOINT;
            role = ConsoleCellRole::Replacement;
            if (bigos::font::lookup_glyph(REPLACEMENT_CODEPOINT, &glyph) == bigos::font::GlyphLookupStatus::Found) {
                if (glyph.width_class == bigos::font::GlyphWidthClass::Full)
                    return 2;
                return 1;
            }

            render_codepoint = QUESTION_CODEPOINT;
            role = ConsoleCellRole::Replacement;
            return 1;
        }

        void clear_cell_at(uint64_t line, uint8_t x) noexcept {
            const uint8_t columns = visible_columns();
            if (x >= columns)
                return;
            ConsoleRenderCell *cells = g_console.lines[line_slot(line)];
            if (cells[x].role == ConsoleCellRole::WideTrailing && x > 0)
                cells[x - 1] = blank_cell();
            if (cells[x].role == ConsoleCellRole::WideLeading && x + 1 < columns &&
                cells[x + 1].role == ConsoleCellRole::WideTrailing)
                cells[x + 1] = blank_cell();
            cells[x] = blank_cell();
        }

        void write_single_cell(uint32_t codepoint, ConsoleCellRole role) noexcept {
            const uint8_t columns = visible_columns();
            clear_cell_at(g_console.current_line, g_console.cursor_x);
            g_console.lines[line_slot(g_console.current_line)][g_console.cursor_x] = single_cell(codepoint, role);
            if (g_console.cursor_x + 1 >= columns) {
                advance_line();
                return;
            }
            ++g_console.cursor_x;
        }

        void write_wide_cell(uint32_t codepoint, ConsoleCellRole role) noexcept {
            const uint8_t columns = visible_columns();
            if (g_console.cursor_x + 1 >= columns) {
                if (g_console.cursor_x < columns)
                    clear_cell_at(g_console.current_line, g_console.cursor_x);
                advance_line();
            }

            clear_cell_at(g_console.current_line, g_console.cursor_x);
            clear_cell_at(g_console.current_line, (uint8_t)(g_console.cursor_x + 1));
            ConsoleRenderCell *cells = g_console.lines[line_slot(g_console.current_line)];
            cells[g_console.cursor_x] = single_cell(codepoint, ConsoleCellRole::WideLeading);
            cells[g_console.cursor_x + 1] = {codepoint, ConsoleCellRole::WideTrailing, CONSOLE_COLOR};
            g_console.cursor_x = (uint8_t)(g_console.cursor_x + 2);
            if (g_console.cursor_x >= columns)
                advance_line();
        }

        void put_codepoint(uint32_t codepoint) noexcept {
            uint32_t render_codepoint = codepoint;
            ConsoleCellRole role = ConsoleCellRole::Single;
            const uint8_t width = codepoint_cell_width(codepoint, render_codepoint, role);
            if (width == 2)
                write_wide_cell(render_codepoint, role);
            else
                write_single_cell(render_codepoint, role);
        }

        void put_tab() noexcept {
            uint8_t spaces = (uint8_t)(TAB_STOP_COLUMNS - (g_console.cursor_x % TAB_STOP_COLUMNS));
            if (spaces == 0)
                spaces = TAB_STOP_COLUMNS;
            for (uint8_t i = 0; i < spaces; ++i)
                put_codepoint(SPACE_CODEPOINT);
        }

        void put_backspace() noexcept {
            if (g_console.cursor_x == 0) {
                if (g_console.current_line <= g_console.oldest_line)
                    return;
                --g_console.current_line;
                g_console.cursor_x = visible_columns();
            }

            --g_console.cursor_x;
            ConsoleRenderCell *cells = g_console.lines[line_slot(g_console.current_line)];
            if (cells[g_console.cursor_x].role == ConsoleCellRole::WideTrailing && g_console.cursor_x > 0) {
                cells[g_console.cursor_x] = blank_cell();
                --g_console.cursor_x;
                cells[g_console.cursor_x] = blank_cell();
                return;
            }
            clear_cell_at(g_console.current_line, g_console.cursor_x);
        }

        void put_control(uint8_t ch) noexcept {
            const bool follow = viewport_at_bottom();

            switch (ch) {
                case '\n':
                    advance_line();
                    break;
                case '\r':
                    g_console.cursor_x = 0;
                    break;
                case '\t':
                    put_tab();
                    break;
                case '\b':
                    put_backspace();
                    break;
                default:
                    break;
            }

            if (follow)
                g_console.viewport_top = bottom_viewport_top();
            else
                clamp_viewport();
            render_viewport();
        }

        void put_decoded_codepoint(uint32_t codepoint) noexcept {
            const bool follow = viewport_at_bottom();
            put_codepoint(codepoint);
            if (follow)
                g_console.viewport_top = bottom_viewport_top();
            else
                clamp_viewport();
            render_viewport();
        }

        void emit_replacement() noexcept {
            put_decoded_codepoint(REPLACEMENT_CODEPOINT);
        }

        void consume_output_byte(uint8_t byte) noexcept {
            for (;;) {
                if (g_console.utf8.remaining == 0) {
                    if (byte < 0x20u) {
                        put_control(byte);
                        return;
                    }
                    if (byte < 0x7fu) {
                        put_decoded_codepoint(byte);
                        return;
                    }
                    if (byte >= 0xc2u && byte <= 0xdfu) {
                        g_console.utf8.codepoint = byte & 0x1fu;
                        g_console.utf8.min_codepoint = 0x80u;
                        g_console.utf8.remaining = 1;
                        return;
                    }
                    if (byte >= 0xe0u && byte <= 0xefu) {
                        g_console.utf8.codepoint = byte & 0x0fu;
                        g_console.utf8.min_codepoint = 0x800u;
                        g_console.utf8.remaining = 2;
                        return;
                    }
                    if (byte >= 0xf0u && byte <= 0xf4u) {
                        g_console.utf8.codepoint = byte & 0x07u;
                        g_console.utf8.min_codepoint = 0x10000u;
                        g_console.utf8.remaining = 3;
                        return;
                    }
                    emit_replacement();
                    return;
                }

                if (!is_utf8_continuation(byte)) {
                    emit_replacement();
                    reset_decoder();
                    continue;
                }

                g_console.utf8.codepoint = (g_console.utf8.codepoint << 6) | (uint32_t)(byte & 0x3fu);
                --g_console.utf8.remaining;
                if (g_console.utf8.remaining != 0)
                    return;

                const uint32_t codepoint = g_console.utf8.codepoint;
                const uint32_t min_codepoint = g_console.utf8.min_codepoint;
                reset_decoder();
                if (codepoint < min_codepoint || !valid_unicode_scalar(codepoint))
                    emit_replacement();
                else
                    put_decoded_codepoint(codepoint);
                return;
            }
        }
    }   // namespace

    void init_console() noexcept {
        init_console_render_backend();
        reset_console_state();
        console_render_backend().clear();
        render_viewport();
        g_console_ready = true;
    }

    bool console_ready() noexcept {
        return g_console_ready;
    }

    void console_put(char ch) noexcept {
        if (!g_console_ready)
            return;

        consume_output_byte((uint8_t)ch);
    }

    void console_write(const char *s) noexcept {
        if (!g_console_ready || s == nullptr)
            return;

        while (*s != 0) {
            console_put(*s);
            ++s;
        }
    }

    bool default_terminal_write(const char *s) noexcept {
        if (!g_console_ready || s == nullptr)
            return false;

        console_write(s);
        return true;
    }

    void console_clear() noexcept {
        if (!g_console_ready)
            return;

        // Clear-screen discards retained runtime scrollback and returns the viewport to bottom.
        reset_console_state();
        console_render_backend().clear();
        render_viewport();
    }

    void console_scroll_page_up() noexcept {
        if (!g_console_ready)
            return;
        const uint64_t step = visible_rows() > 1 ? visible_rows() - 1 : 1;
        if (g_console.viewport_top <= g_console.oldest_line + step)
            g_console.viewport_top = g_console.oldest_line;
        else
            g_console.viewport_top -= step;
        render_viewport();
    }

    void console_scroll_page_down() noexcept {
        if (!g_console_ready)
            return;
        const uint64_t bottom = bottom_viewport_top();
        const uint64_t step = visible_rows() > 1 ? visible_rows() - 1 : 1;
        if (bottom <= g_console.viewport_top + step)
            g_console.viewport_top = bottom;
        else
            g_console.viewport_top += step;
        render_viewport();
    }

    void console_scroll_home() noexcept {
        if (!g_console_ready)
            return;
        g_console.viewport_top = g_console.oldest_line;
        render_viewport();
    }

    void console_scroll_end() noexcept {
        if (!g_console_ready)
            return;
        g_console.viewport_top = bottom_viewport_top();
        render_viewport();
    }
}   // namespace bigos::terminal
