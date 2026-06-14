#ifndef BIGOS_TTY_H
#define BIGOS_TTY_H

#include <bigos/types.h>
#include <bigos/timer.h>

namespace bigos::terminal {
    constexpr size_t TTY_INPUT_CAPACITY = 128;

    enum class TerminalInputKind : uint8_t {
        Character,
        Control,
    };

    enum class TerminalControl : uint8_t {
        None,
        LineEnd,
        Backspace,
        DeleteLike,
        EofLike,
        InterruptLike,
        Unsupported,
    };

    struct TerminalInputRecord {
        TerminalInputKind kind;
        char ch;
        TerminalControl control;
    };

    struct TTYInputStats {
        uint64_t dropped;
    };

    void init_tty() noexcept;

    TerminalControl classify_control_char(char ch) noexcept;

    // IRQ-context producer API. Returns false when the fixed input buffer is full.
    bool enqueue_input(char ch) noexcept;
    bool enqueue_input_record(const TerminalInputRecord &record) noexcept;

    // Non-interrupt consumer APIs. Empty input returns false/0 without blocking.
    bool read_input_record(TerminalInputRecord *out) noexcept;
    bool read_char(char *out) noexcept;
    size_t drain(char *out, size_t capacity) noexcept;
    // Non-interrupt ordinary thread context only. Blocks until a character is
    // available, the timeout expires, or the scheduler rejects the context.
    // Returns 1 when a character is written to out, 0 for the bounded EOF-like
    // terminal event, or a negative wait error.
    int read_char_blocking(char *out, timer::tick_t timeout_ticks = 0) noexcept;
    int read_input_record_blocking(TerminalInputRecord *out, timer::tick_t timeout_ticks = 0) noexcept;
    TTYInputStats input_stats() noexcept;
}   // namespace bigos::terminal

#endif   // BIGOS_TTY_H
