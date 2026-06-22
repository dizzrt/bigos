#ifndef BIGOS_CONSOLE_H
#define BIGOS_CONSOLE_H

#include <bigos/types.h>

namespace bigos::terminal {
    constexpr char ASCII_BACKSPACE = '\b';
    constexpr char ASCII_ESCAPE = 0x1b;

    void init_console() noexcept;
    bool console_ready() noexcept;

    // Ordinary runtime console output. Early diagnostics continue to use kput/kputs.
    void console_put(char ch) noexcept;
    void console_write(const char *s) noexcept;
    void console_clear() noexcept;
    void console_scroll_page_up() noexcept;
    void console_scroll_page_down() noexcept;
    void console_scroll_home() noexcept;
    void console_scroll_end() noexcept;
    bool default_terminal_write(const char *s) noexcept;
}   // namespace bigos::terminal

#endif   // BIGOS_CONSOLE_H
