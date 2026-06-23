#ifndef _BIGOS_CONSOLE_RENDER_H
#define _BIGOS_CONSOLE_RENDER_H

#include <bigos/types.h>

namespace bigos::terminal {
    constexpr uint32_t CONSOLE_RENDER_WIDTH = 80;
    constexpr uint32_t CONSOLE_RENDER_HEIGHT = 25;

    struct ConsoleRenderCell {
        char ch;
        uint8_t color;
    };

    struct ConsoleRenderBackend {
        const char *name;
        void (*clear)() noexcept;
        void (*begin_viewport_redraw)() noexcept;
        void (*draw_cell)(uint8_t __x, uint8_t __y, const ConsoleRenderCell &__cell) noexcept;
        void (*end_viewport_redraw)() noexcept;
        void (*set_cursor)(uint8_t __x, uint8_t __y, bool __visible, const ConsoleRenderCell &__cell) noexcept;
    };

    // Kernel-internal runtime console render boundary. This is not a user-visible
    // terminal API and does not add ANSI/VT, termios, multi-terminal, UTF-8, CJK,
    // codepoint-cell, or full graphical terminal semantics.
    void init_console_render_backend() noexcept;
    const ConsoleRenderBackend &console_render_backend() noexcept;
}   // namespace bigos::terminal

#endif   // _BIGOS_CONSOLE_RENDER_H
