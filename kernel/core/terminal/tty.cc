#include <bigos/console.h>
#include <bigos/io.h>
#include <bigos/keyboard.h>
#include <bigos/sched.h>
#include <bigos/tty.h>

namespace bigos::terminal {
    namespace {
        struct InputRing {
            TerminalInputRecord buffer[TTY_INPUT_CAPACITY];
            volatile size_t head;
            volatile size_t tail;
            volatile uint64_t dropped;
        };

        InputRing g_input;
        sched::WaitQueue g_input_wait;

        size_t next_index(size_t index) noexcept {
            return (index + 1) % TTY_INPUT_CAPACITY;
        }

        bool input_available(void *) noexcept {
            return g_input.tail != g_input.head;
        }

        TerminalInputRecord make_record(char ch) noexcept {
            const TerminalControl control = classify_control_char(ch);
            if (control == TerminalControl::None) {
                return {TerminalInputKind::Character, ch, TerminalControl::None};
            }
            return {TerminalInputKind::Control, ch, control};
        }

        enum class ConsumeResult : uint8_t {
            NoData,
            Character,
            Eof,
            Ignored,
        };

        ConsumeResult consume_record_as_char(const TerminalInputRecord &record, char *out) noexcept {
            if (out == nullptr)
                return ConsumeResult::NoData;

            if (record.kind == TerminalInputKind::Character) {
                *out = record.ch;
                return ConsumeResult::Character;
            }

            switch (record.control) {
                case TerminalControl::LineEnd:
                    *out = '\n';
                    return ConsumeResult::Character;
                case TerminalControl::Backspace:
                    *out = '\b';
                    return ConsumeResult::Character;
                case TerminalControl::DeleteLike:
                    *out = 0x7f;
                    return ConsumeResult::Character;
                case TerminalControl::EofLike:
                    return ConsumeResult::Eof;
                case TerminalControl::InterruptLike:
                    *out = 0x03;
                    return ConsumeResult::Character;
                case TerminalControl::Unsupported:
                    return ConsumeResult::Ignored;
                case TerminalControl::None:
                    break;
            }

            *out = record.ch;
            return ConsumeResult::Character;
        }
    }   // namespace

    void init_tty() noexcept {
        g_input.head = 0;
        g_input.tail = 0;
        g_input.dropped = 0;
        sched::init_wait_queue(&g_input_wait);
        init_console();
        input::init_keyboard_decoder();
    }

    TerminalControl classify_control_char(char ch) noexcept {
        switch ((unsigned char)ch) {
            case '\n':
            case '\r':
                return TerminalControl::LineEnd;
            case '\b':
                return TerminalControl::Backspace;
            case 0x7f:
                return TerminalControl::DeleteLike;
            case 0x04:
                return TerminalControl::EofLike;
            case 0x03:
                return TerminalControl::InterruptLike;
            case '\t':
                return TerminalControl::None;
            default:
                break;
        }

        const unsigned char byte = (unsigned char)ch;
        if (byte < ' ' || byte == 0x7f)
            return TerminalControl::Unsupported;
        return TerminalControl::None;
    }

    bool enqueue_input(char ch) noexcept {
        return enqueue_input_record(make_record(ch));
    }

    bool enqueue_input_record(const TerminalInputRecord &record) noexcept {
        const size_t head = g_input.head;
        const size_t next = next_index(head);
        if (next == g_input.tail) {
            ++g_input.dropped;
            return false;
        }

        g_input.buffer[head] = record;
        g_input.head = next;
        sched::wake_one(&g_input_wait);
        return true;
    }

    bool read_input_record(TerminalInputRecord *out) noexcept {
        if (out == nullptr)
            return false;

        const size_t tail = g_input.tail;
        if (tail == g_input.head)
            return false;

        *out = g_input.buffer[tail];
        g_input.tail = next_index(tail);
        return true;
    }

    bool read_char(char *out) noexcept {
        TerminalInputRecord record{};
        while (read_input_record(&record)) {
            const ConsumeResult result = consume_record_as_char(record, out);
            if (result == ConsumeResult::Character)
                return true;
            if (result == ConsumeResult::Eof)
                return false;
        }
        return false;
    }

    size_t drain(char *out, size_t capacity) noexcept {
        if (out == nullptr)
            return 0;

        size_t count = 0;
        while (count < capacity && read_char(&out[count]))
            ++count;
        return count;
    }

    int read_input_record_blocking(TerminalInputRecord *out, timer::tick_t timeout_ticks) noexcept {
        if (out == nullptr)
            return sched::WAIT_INVALID;

        while (true) {
            if (read_input_record(out)) {
                return 1;
            }

            const int wait = sched::wait_queue_wait_until(&g_input_wait, &input_available, nullptr, timeout_ticks);
            if (wait < 0)
                return wait;
        }
    }

    int read_char_blocking(char *out, timer::tick_t timeout_ticks) noexcept {
        if (out == nullptr)
            return sched::WAIT_INVALID;

        while (true) {
            TerminalInputRecord record{};
            const int read = read_input_record_blocking(&record, timeout_ticks);
            if (read < 0)
                return read;

            const ConsumeResult result = consume_record_as_char(record, out);
            if (result == ConsumeResult::Character)
                return 1;
            if (result == ConsumeResult::Eof)
                return 0;
        }
    }

    TTYInputStats input_stats() noexcept {
        return {g_input.dropped};
    }
}   // namespace bigos::terminal
