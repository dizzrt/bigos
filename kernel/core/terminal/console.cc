#include <bigos/console.h>
#include <bigos/console_render.h>
#include <bigos/glyph_font.h>

namespace bigos::terminal {
    namespace {
        constexpr uint32_t CONSOLE_SCROLLBACK_LINES = 256;
        constexpr ConsoleDisplayAttr DEFAULT_ATTR = {7, 0};
        constexpr uint32_t REPLACEMENT_CODEPOINT = 0xfffd;
        constexpr uint32_t QUESTION_CODEPOINT = '?';
        constexpr uint32_t SPACE_CODEPOINT = ' ';
        constexpr uint8_t TAB_STOP_COLUMNS = 4;
        constexpr uint8_t CSI_MAX_PARAMS = 8;
        constexpr uint16_t CSI_PARAM_UNSET = 0xffffu;
        constexpr uint16_t CSI_PARAM_MAX = 999u;

        struct Utf8Decoder {
            uint32_t codepoint;
            uint32_t min_codepoint;
            uint8_t remaining;
        };

        enum class VtParserState : uint8_t {
            Ground,
            Escape,
            Csi,
        };

        struct VtParser {
            VtParserState state;
            uint16_t params[CSI_MAX_PARAMS];
            uint8_t param_count;
            bool current_param_active;
            bool overflow;
        };

        struct ConsoleState {
            ConsoleRenderCell lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_RENDER_MAX_WIDTH];
            uint64_t oldest_line;
            uint64_t current_line;
            uint64_t viewport_top;
            uint8_t visible_columns;
            uint8_t visible_rows;
            uint8_t cursor_x;
            ConsoleDisplayAttr current_attr;
            uint8_t saved_cursor_x;
            uint8_t saved_cursor_y;
            bool saved_cursor_valid;
            Utf8Decoder utf8;
            VtParser parser;
            bool initialized;
        };

        bool g_console_ready = false;
        ConsoleState g_console;

        ConsoleRenderCell blank_cell(ConsoleDisplayAttr attr = DEFAULT_ATTR) noexcept {
            return {SPACE_CODEPOINT, ConsoleCellRole::Blank, attr};
        }

        ConsoleRenderCell single_cell(uint32_t codepoint, ConsoleCellRole role = ConsoleCellRole::Single) noexcept {
            return {codepoint, role, g_console.current_attr};
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
            g_console.current_attr = DEFAULT_ATTR;
            g_console.saved_cursor_x = 0;
            g_console.saved_cursor_y = 0;
            g_console.saved_cursor_valid = false;
            g_console.utf8 = {};
            g_console.parser = {};
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
            cells[g_console.cursor_x + 1] = {codepoint, ConsoleCellRole::WideTrailing, g_console.current_attr};
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

        uint8_t clamp_column(uint32_t column) noexcept {
            const uint8_t columns = visible_columns();
            if (columns == 0)
                return 0;
            if (column >= columns)
                return (uint8_t)(columns - 1);
            return (uint8_t)column;
        }

        uint8_t clamp_row(uint32_t row) noexcept {
            const uint8_t rows = visible_rows();
            if (rows == 0)
                return 0;
            if (row >= rows)
                return (uint8_t)(rows - 1);
            return (uint8_t)row;
        }

        uint8_t cursor_visible_row() noexcept {
            clamp_viewport();
            if (g_console.current_line <= g_console.viewport_top)
                return 0;
            const uint64_t row = g_console.current_line - g_console.viewport_top;
            return clamp_row((uint32_t)row);
        }

        void ensure_line_exists(uint64_t line) noexcept {
            while (g_console.current_line < line)
                advance_line();
        }

        void set_cursor_visible(uint8_t row, uint8_t column) noexcept {
            const bool follow = viewport_at_bottom();
            if (follow)
                g_console.viewport_top = bottom_viewport_top();
            else
                clamp_viewport();

            const uint64_t target_line = g_console.viewport_top + clamp_row(row);
            ensure_line_exists(target_line);
            g_console.current_line = target_line;
            g_console.cursor_x = clamp_column(column);
        }

        void move_cursor_relative(int32_t delta_row, int32_t delta_column) noexcept {
            int32_t row = (int32_t)cursor_visible_row() + delta_row;
            int32_t column = (int32_t)g_console.cursor_x + delta_column;
            if (row < 0)
                row = 0;
            if (column < 0)
                column = 0;
            set_cursor_visible(clamp_row((uint32_t)row), clamp_column((uint32_t)column));
        }

        void clear_visible_cell(uint8_t row, uint8_t column) noexcept {
            const uint64_t line = g_console.viewport_top + row;
            if (line < g_console.oldest_line || line > g_console.current_line)
                return;
            ConsoleRenderCell *cells = g_console.lines[line_slot(line)];
            const uint8_t columns = visible_columns();
            if (column >= columns)
                return;
            if (cells[column].role == ConsoleCellRole::WideTrailing && column > 0)
                cells[column - 1] = blank_cell(g_console.current_attr);
            if (cells[column].role == ConsoleCellRole::WideLeading && column + 1 < columns &&
                cells[column + 1].role == ConsoleCellRole::WideTrailing)
                cells[column + 1] = blank_cell(g_console.current_attr);
            cells[column] = blank_cell(g_console.current_attr);
        }

        void erase_line(uint8_t row, uint8_t start_column, uint8_t end_column) noexcept {
            const uint8_t columns = visible_columns();
            if (start_column >= columns)
                return;
            if (end_column >= columns)
                end_column = (uint8_t)(columns - 1);
            for (uint8_t column = start_column; column <= end_column; ++column)
                clear_visible_cell(row, column);
        }

        void erase_display(uint16_t mode) noexcept {
            const uint8_t rows = visible_rows();
            const uint8_t columns = visible_columns();
            const uint8_t row = cursor_visible_row();
            const uint8_t column = g_console.cursor_x;
            if (mode == 2) {
                for (uint8_t y = 0; y < rows; ++y)
                    erase_line(y, 0, (uint8_t)(columns - 1));
                return;
            }
            if (mode == 1) {
                for (uint8_t y = 0; y < row; ++y)
                    erase_line(y, 0, (uint8_t)(columns - 1));
                erase_line(row, 0, column);
                return;
            }
            erase_line(row, column, (uint8_t)(columns - 1));
            for (uint8_t y = (uint8_t)(row + 1); y < rows; ++y)
                erase_line(y, 0, (uint8_t)(columns - 1));
        }

        void erase_current_line(uint16_t mode) noexcept {
            const uint8_t columns = visible_columns();
            const uint8_t row = cursor_visible_row();
            const uint8_t column = g_console.cursor_x;
            if (mode == 2)
                erase_line(row, 0, (uint8_t)(columns - 1));
            else if (mode == 1)
                erase_line(row, 0, column);
            else
                erase_line(row, column, (uint8_t)(columns - 1));
        }

        void reset_vt_parser() noexcept {
            g_console.parser = {};
        }

        uint16_t csi_param(uint8_t index, uint16_t default_value) noexcept {
            if (index >= g_console.parser.param_count || g_console.parser.params[index] == CSI_PARAM_UNSET)
                return default_value;
            return g_console.parser.params[index];
        }

        uint16_t csi_count_param(uint8_t index) noexcept {
            const uint16_t value = csi_param(index, 1);
            return value == 0 ? 1 : value;
        }

        void apply_sgr() noexcept {
            if (g_console.parser.param_count == 0) {
                g_console.current_attr = DEFAULT_ATTR;
                return;
            }
            for (uint8_t i = 0; i < g_console.parser.param_count; ++i) {
                const uint16_t param = csi_param(i, 0);
                if (param == 0) {
                    g_console.current_attr = DEFAULT_ATTR;
                } else if (param == 39) {
                    g_console.current_attr.foreground = DEFAULT_ATTR.foreground;
                } else if (param == 49) {
                    g_console.current_attr.background = DEFAULT_ATTR.background;
                } else if (param >= 30 && param <= 37) {
                    g_console.current_attr.foreground = (uint8_t)(param - 30);
                } else if (param >= 40 && param <= 47) {
                    g_console.current_attr.background = (uint8_t)(param - 40);
                } else if (param >= 90 && param <= 97) {
                    g_console.current_attr.foreground = (uint8_t)(8 + param - 90);
                } else if (param >= 100 && param <= 107) {
                    g_console.current_attr.background = (uint8_t)(8 + param - 100);
                }
            }
        }

        void save_cursor() noexcept {
            g_console.saved_cursor_x = g_console.cursor_x;
            g_console.saved_cursor_y = cursor_visible_row();
            g_console.saved_cursor_valid = true;
        }

        void restore_cursor() noexcept {
            if (g_console.saved_cursor_valid)
                set_cursor_visible(g_console.saved_cursor_y, g_console.saved_cursor_x);
            else
                set_cursor_visible(0, 0);
        }

        void apply_csi_final(uint8_t final_byte) noexcept {
            switch (final_byte) {
                case 'A':
                    move_cursor_relative(-(int32_t)csi_count_param(0), 0);
                    break;
                case 'B':
                    move_cursor_relative((int32_t)csi_count_param(0), 0);
                    break;
                case 'C':
                    move_cursor_relative(0, (int32_t)csi_count_param(0));
                    break;
                case 'D':
                    move_cursor_relative(0, -(int32_t)csi_count_param(0));
                    break;
                case 'H':
                case 'f': {
                    const uint16_t row = csi_count_param(0);
                    const uint16_t column = csi_count_param(1);
                    set_cursor_visible(clamp_row((uint32_t)(row - 1)), clamp_column((uint32_t)(column - 1)));
                    break;
                }
                case 'J':
                    erase_display(csi_param(0, 0));
                    break;
                case 'K':
                    erase_current_line(csi_param(0, 0));
                    break;
                case 'm':
                    apply_sgr();
                    break;
                case 's':
                    save_cursor();
                    break;
                case 'u':
                    restore_cursor();
                    break;
                default:
                    break;
            }
        }

        void consume_csi_byte(uint8_t byte) noexcept {
            if (byte >= '0' && byte <= '9') {
                if (g_console.parser.param_count == 0) {
                    g_console.parser.param_count = 1;
                    g_console.parser.params[0] = 0;
                }
                uint16_t &param = g_console.parser.params[g_console.parser.param_count - 1];
                if (param == CSI_PARAM_UNSET)
                    param = 0;
                const uint16_t digit = (uint16_t)(byte - '0');
                if (param > (CSI_PARAM_MAX - digit) / 10)
                    g_console.parser.overflow = true;
                else
                    param = (uint16_t)(param * 10 + digit);
                g_console.parser.current_param_active = true;
                return;
            }
            if (byte == ';') {
                if (g_console.parser.param_count == 0) {
                    g_console.parser.param_count = 1;
                    g_console.parser.params[0] = CSI_PARAM_UNSET;
                }
                if (g_console.parser.param_count >= CSI_MAX_PARAMS) {
                    g_console.parser.overflow = true;
                    return;
                }
                g_console.parser.params[g_console.parser.param_count++] = CSI_PARAM_UNSET;
                g_console.parser.current_param_active = false;
                return;
            }
            if (byte >= 0x40u && byte <= 0x7eu) {
                if (!g_console.parser.overflow)
                    apply_csi_final(byte);
                reset_vt_parser();
                render_viewport();
                return;
            }
            reset_vt_parser();
        }

        void consume_output_byte(uint8_t byte) noexcept {
            if (g_console.parser.state == VtParserState::Escape) {
                if (byte == '[') {
                    reset_decoder();
                    g_console.parser.state = VtParserState::Csi;
                    for (uint8_t i = 0; i < CSI_MAX_PARAMS; ++i)
                        g_console.parser.params[i] = CSI_PARAM_UNSET;
                    return;
                }
                if (byte == '7') {
                    save_cursor();
                    reset_vt_parser();
                    render_viewport();
                    return;
                }
                if (byte == '8') {
                    restore_cursor();
                    reset_vt_parser();
                    render_viewport();
                    return;
                }
                reset_vt_parser();
                return;
            }

            if (g_console.parser.state == VtParserState::Csi) {
                consume_csi_byte(byte);
                return;
            }

            if (byte == 0x1bu) {
                reset_decoder();
                g_console.parser.state = VtParserState::Escape;
                return;
            }

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
