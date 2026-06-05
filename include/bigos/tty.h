#ifndef BIGOS_TTY_H
#define BIGOS_TTY_H

#include <bigos/types.h>

namespace bigos::terminal {
    constexpr size_t TTY_INPUT_CAPACITY = 128;

    struct TTYInputStats {
        uint64_t dropped;
    };

    void init_tty() noexcept;

    // IRQ-context producer API. Returns false when the fixed input buffer is full.
    bool enqueue_input(char ch) noexcept;

    // Non-interrupt consumer APIs. Empty input returns false/0 without blocking.
    bool read_char(char *out) noexcept;
    size_t drain(char *out, size_t capacity) noexcept;
    TTYInputStats input_stats() noexcept;
}   // namespace bigos::terminal

#endif   // BIGOS_TTY_H
