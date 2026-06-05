#include <bigos/console.h>
#include <bigos/keyboard.h>
#include <bigos/tty.h>

namespace bigos::terminal {
    namespace {
        struct InputRing {
            char buffer[TTY_INPUT_CAPACITY];
            volatile size_t head;
            volatile size_t tail;
            volatile uint64_t dropped;
        };

        InputRing g_input;

        size_t next_index(size_t index) noexcept {
            return (index + 1) % TTY_INPUT_CAPACITY;
        }
    }   // namespace

    void init_tty() noexcept {
        g_input.head = 0;
        g_input.tail = 0;
        g_input.dropped = 0;
        init_console();
        input::init_keyboard_decoder();
    }

    bool enqueue_input(char ch) noexcept {
        const size_t head = g_input.head;
        const size_t next = next_index(head);
        if (next == g_input.tail) {
            ++g_input.dropped;
            return false;
        }

        g_input.buffer[head] = ch;
        g_input.head = next;
        return true;
    }

    bool read_char(char *out) noexcept {
        if (out == nullptr)
            return false;

        const size_t tail = g_input.tail;
        if (tail == g_input.head)
            return false;

        *out = g_input.buffer[tail];
        g_input.tail = next_index(tail);
        return true;
    }

    size_t drain(char *out, size_t capacity) noexcept {
        if (out == nullptr)
            return 0;

        size_t count = 0;
        while (count < capacity && read_char(&out[count]))
            ++count;
        return count;
    }

    TTYInputStats input_stats() noexcept {
        return {g_input.dropped};
    }
}   // namespace bigos::terminal
