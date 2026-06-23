#ifndef _BIGOS_CONSOLE_RENDER_H
#define _BIGOS_CONSOLE_RENDER_H

#include <bigos/types.h>

namespace bigos::terminal {
    constexpr uint8_t CONSOLE_RENDER_VGA_WIDTH = 80;
    constexpr uint8_t CONSOLE_RENDER_VGA_HEIGHT = 25;
    constexpr uint8_t CONSOLE_RENDER_MIN_WIDTH = CONSOLE_RENDER_VGA_WIDTH;
    constexpr uint8_t CONSOLE_RENDER_MIN_HEIGHT = CONSOLE_RENDER_VGA_HEIGHT;
    constexpr uint8_t CONSOLE_RENDER_MAX_WIDTH = 240;
    constexpr uint8_t CONSOLE_RENDER_MAX_HEIGHT = 80;

    enum class ConsoleCellRole : uint8_t {
        Blank,
        Single,
        WideLeading,
        WideTrailing,
        Replacement,
    };

    struct ConsoleRenderCell {
        uint32_t codepoint;
        ConsoleCellRole role;
        uint8_t color;
    };

    struct ConsoleRenderBackend {
        const char *name;
        uint8_t visible_columns;
        uint8_t visible_rows;
        void (*clear)() noexcept;
        void (*begin_viewport_redraw)() noexcept;
        void (*draw_cell)(uint8_t __x, uint8_t __y, const ConsoleRenderCell &__cell) noexcept;
        void (*end_viewport_redraw)() noexcept;
        void (*set_cursor)(uint8_t __x, uint8_t __y, bool __visible, const ConsoleRenderCell &__cell) noexcept;
    };

    // Kernel-internal runtime console render boundary. This is not a user-visible
    // terminal API and does not add ANSI/VT, termios, multi-terminal, locale,
    // shaping, or complete graphical terminal semantics.
    void init_console_render_backend() noexcept;
    const ConsoleRenderBackend &console_render_backend() noexcept;
}   // namespace bigos::terminal

#endif   // _BIGOS_CONSOLE_RENDER_H
