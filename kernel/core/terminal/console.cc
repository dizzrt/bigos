#include <bigos/console.h>
#include <bigos/device.h>

namespace bigos::terminal {
    namespace {
        constexpr uint32_t CONSOLE_WIDTH = 80;
        constexpr uint32_t CONSOLE_HEIGHT = 25;
        constexpr uint32_t CONSOLE_SCROLLBACK_LINES = 256;
        constexpr uint8_t CONSOLE_COLOR = 0x0f;

        struct ConsoleCell {
            char ch;
            uint8_t color;
        };

        struct ConsoleState {
            ConsoleCell lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_WIDTH];
            uint64_t oldest_line;
            uint64_t current_line;
            uint64_t viewport_top;
            uint8_t cursor_x;
            bool initialized;
        };

        bool g_console_ready = false;
        ConsoleState g_console;

        uint32_t line_slot(uint64_t line) noexcept {
            return (uint32_t)(line % CONSOLE_SCROLLBACK_LINES);
        }

        void clear_line(uint64_t line) noexcept {
            ConsoleCell *cells = g_console.lines[line_slot(line)];
            for (uint32_t x = 0; x < CONSOLE_WIDTH; ++x) {
                cells[x].ch = ' ';
                cells[x].color = CONSOLE_COLOR;
            }
        }

        uint64_t bottom_viewport_top() noexcept {
            const uint64_t natural_top = g_console.current_line + 1 > CONSOLE_HEIGHT
                ? g_console.current_line + 1 - CONSOLE_HEIGHT
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

            clamp_viewport();
            for (uint32_t y = 0; y < CONSOLE_HEIGHT; ++y) {
                const uint64_t line = g_console.viewport_top + y;
                for (uint32_t x = 0; x < CONSOLE_WIDTH; ++x) {
                    if (line >= g_console.oldest_line && line <= g_console.current_line) {
                        const ConsoleCell &cell = g_console.lines[line_slot(line)][x];
                        bigos::device::fill_video_text_cell((uint8_t)x, (uint8_t)y, cell.ch, cell.color);
                    } else {
                        bigos::device::fill_video_text_cell((uint8_t)x, (uint8_t)y, ' ', CONSOLE_COLOR);
                    }
                }
            }

            const uint64_t cursor_screen_line = g_console.current_line >= g_console.viewport_top
                ? g_console.current_line - g_console.viewport_top
                : 0;
            const uint8_t cursor_y = cursor_screen_line < CONSOLE_HEIGHT
                ? (uint8_t)cursor_screen_line
                : (uint8_t)(CONSOLE_HEIGHT - 1);
            bigos::device::set_video_text_cursor(g_console.cursor_x, cursor_y);
        }

        void reset_console_state() noexcept {
            g_console.oldest_line = 0;
            g_console.current_line = 0;
            g_console.viewport_top = 0;
            g_console.cursor_x = 0;
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

        void put_printable(char ch) noexcept {
            g_console.lines[line_slot(g_console.current_line)][g_console.cursor_x] = {ch, CONSOLE_COLOR};
            if (g_console.cursor_x + 1 >= CONSOLE_WIDTH) {
                advance_line();
                return;
            }
            ++g_console.cursor_x;
        }

        void put_backspace() noexcept {
            if (g_console.cursor_x > 0) {
                --g_console.cursor_x;
            } else if (g_console.current_line > g_console.oldest_line) {
                --g_console.current_line;
                g_console.cursor_x = (uint8_t)(CONSOLE_WIDTH - 1);
            } else {
                return;
            }
            g_console.lines[line_slot(g_console.current_line)][g_console.cursor_x] = {' ', CONSOLE_COLOR};
        }

        void put_char(char ch) noexcept {
            const bool follow = viewport_at_bottom();

            switch (ch) {
                case '\n':
                    advance_line();
                    break;
                case '\r':
                    g_console.cursor_x = 0;
                    break;
                case '\t':
                    for (uint8_t i = 0; i < 4; ++i)
                        put_printable(' ');
                    break;
                case '\b':
                    put_backspace();
                    break;
                default:
                    put_printable(ch);
                    break;
            }

            if (follow)
                g_console.viewport_top = bottom_viewport_top();
            else
                clamp_viewport();
            render_viewport();
        }
    }   // namespace

    void init_console() noexcept {
        reset_console_state();
        render_viewport();
        g_console_ready = true;
    }

    bool console_ready() noexcept {
        return g_console_ready;
    }

    void console_put(char ch) noexcept {
        if (!g_console_ready)
            return;

        put_char(ch);
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
        render_viewport();
    }

    void console_scroll_page_up() noexcept {
        if (!g_console_ready)
            return;
        const uint64_t step = CONSOLE_HEIGHT - 1;
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
        const uint64_t step = CONSOLE_HEIGHT - 1;
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
