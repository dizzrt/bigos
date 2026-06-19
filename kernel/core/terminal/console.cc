#include <bigos/console.h>
#include <bigos/device.h>

namespace bigos::terminal {
    namespace {
        bool g_console_ready = false;
    }   // namespace

    void init_console() noexcept {
        g_console_ready = true;
    }

    bool console_ready() noexcept {
        return g_console_ready;
    }

    void console_put(char ch) noexcept {
        if (!g_console_ready)
            return;

        bigos::device::write_video_text(ch);
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
}   // namespace bigos::terminal
