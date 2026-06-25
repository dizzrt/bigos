#include <bigos/console.h>
#include <bigos/errno.h>
#include <bigos/io.h>
#include <bigos/keyboard.h>
#include <bigos/proc.h>
#include <bigos/sched.h>
#include <bigos/signal.h>
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
        uint32_t g_foreground_pgid = 0;
        TerminalInputMode g_input_mode = TerminalInputMode::Canonical;

        struct RawSequenceState {
            char bytes[4];
            uint8_t len;
            uint8_t index;
        };

        RawSequenceState g_raw_sequence;

        size_t next_index(size_t index) noexcept {
            return (index + 1) % TTY_INPUT_CAPACITY;
        }

        bool input_available(void *) noexcept {
            return g_raw_sequence.index < g_raw_sequence.len || g_input.tail != g_input.head;
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
                    if (g_foreground_pgid != 0)
                        (void)bigos::proc::signal_process_group_from_current(g_foreground_pgid, bigos::signal::SIGINT);
                    *out = 0x03;
                    return ConsumeResult::Character;
                case TerminalControl::ScrollPageUp:
                    console_scroll_page_up();
                    return ConsumeResult::Ignored;
                case TerminalControl::ScrollPageDown:
                    console_scroll_page_down();
                    return ConsumeResult::Ignored;
                case TerminalControl::ScrollHome:
                    console_scroll_home();
                    return ConsumeResult::Ignored;
                case TerminalControl::ScrollEnd:
                    console_scroll_end();
                    return ConsumeResult::Ignored;
                case TerminalControl::Unsupported:
                    return ConsumeResult::Ignored;
                case TerminalControl::None:
                    break;
            }

            *out = record.ch;
            return ConsumeResult::Character;
        }

        void set_raw_sequence(const char *bytes, uint8_t len) noexcept {
            g_raw_sequence.len = len;
            g_raw_sequence.index = 0;
            for (uint8_t i = 0; i < len && i < sizeof(g_raw_sequence.bytes); ++i)
                g_raw_sequence.bytes[i] = bytes[i];
        }

        bool take_raw_sequence_byte(char *out) noexcept {
            if (out == nullptr || g_raw_sequence.index >= g_raw_sequence.len)
                return false;
            *out = g_raw_sequence.bytes[g_raw_sequence.index++];
            if (g_raw_sequence.index >= g_raw_sequence.len) {
                g_raw_sequence.index = 0;
                g_raw_sequence.len = 0;
            }
            return true;
        }

        bool consume_record_as_raw_char(const TerminalInputRecord &record, char *out) noexcept {
            if (out == nullptr)
                return false;

            if (record.kind == TerminalInputKind::Character) {
                *out = record.ch;
                return true;
            }

            switch (record.control) {
                case TerminalControl::LineEnd:
                    *out = '\n';
                    return true;
                case TerminalControl::Backspace:
                    *out = '\b';
                    return true;
                case TerminalControl::DeleteLike:
                    *out = 0x7f;
                    return true;
                case TerminalControl::EofLike:
                    *out = 0x04;
                    return true;
                case TerminalControl::InterruptLike:
                    *out = 0x03;
                    return true;
                case TerminalControl::ScrollPageUp:
                    set_raw_sequence("\x1b[5~", 4);
                    return take_raw_sequence_byte(out);
                case TerminalControl::ScrollPageDown:
                    set_raw_sequence("\x1b[6~", 4);
                    return take_raw_sequence_byte(out);
                case TerminalControl::ScrollHome:
                    set_raw_sequence("\x1b[H", 3);
                    return take_raw_sequence_byte(out);
                case TerminalControl::ScrollEnd:
                    set_raw_sequence("\x1b[F", 3);
                    return take_raw_sequence_byte(out);
                case TerminalControl::Unsupported:
                    return false;
                case TerminalControl::None:
                    break;
            }

            *out = record.ch;
            return true;
        }

        bool mode_object_valid(const TerminalMode &mode, TerminalInputMode *out) noexcept {
            if (mode.size != sizeof(TerminalMode) || mode.version != TERMINAL_MODE_ABI_VERSION ||
                mode.flags != TERMINAL_MODE_FLAG_NONE || out == nullptr)
                return false;
            if (mode.mode == TERMINAL_MODE_CANONICAL) {
                *out = TerminalInputMode::Canonical;
                return true;
            }
            if (mode.mode == TERMINAL_MODE_RAW) {
                *out = TerminalInputMode::Raw;
                return true;
            }
            return false;
        }

        bool current_process_may_set_mode(TerminalInputMode requested) noexcept {
            bigos::proc::Process *process = bigos::proc::current_process();
            if (process == nullptr)
                return false;

            const uint32_t fg = foreground_pgid();
            if (fg != 0 && process->pgid == fg)
                return bigos::proc::process_group_exists_in_session(fg, process->sid);

            return requested == TerminalInputMode::Canonical && process->sid == process->pid;
        }
    }   // namespace

    void init_tty() noexcept {
        g_input.head = 0;
        g_input.tail = 0;
        g_input.dropped = 0;
        g_input_mode = TerminalInputMode::Canonical;
        g_raw_sequence = {};
        g_foreground_pgid = 0;
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

    TerminalInputMode input_mode() noexcept {
        return g_input_mode;
    }

    int64_t get_input_mode(TerminalMode *out) noexcept {
        if (out == nullptr)
            return -bigos::EINVAL;
        out->size = sizeof(TerminalMode);
        out->version = TERMINAL_MODE_ABI_VERSION;
        out->mode = g_input_mode == TerminalInputMode::Raw ? TERMINAL_MODE_RAW : TERMINAL_MODE_CANONICAL;
        out->flags = TERMINAL_MODE_FLAG_NONE;
        return 0;
    }

    int64_t set_input_mode(const TerminalMode &mode) noexcept {
        TerminalInputMode requested = TerminalInputMode::Canonical;
        if (!mode_object_valid(mode, &requested))
            return -bigos::EINVAL;
        if (!current_process_may_set_mode(requested))
            return -bigos::EPERM;
        g_input_mode = requested;
        g_raw_sequence = {};
        return 0;
    }

    int64_t force_canonical_input_mode() noexcept {
        g_input_mode = TerminalInputMode::Canonical;
        g_raw_sequence = {};
        return 0;
    }

    uint32_t foreground_pgid() noexcept {
        if (g_foreground_pgid != 0 && !bigos::proc::process_group_has_live_member(g_foreground_pgid))
            g_foreground_pgid = 0;
        return g_foreground_pgid;
    }

    int64_t set_foreground_pgid(uint32_t pgid) noexcept {
        if (pgid == 0)
            return -bigos::EINVAL;
        bigos::proc::Process *process = bigos::proc::current_process();
        if (process == nullptr)
            return -bigos::ESRCH;
        if (!bigos::proc::process_group_exists_in_session(pgid, process->sid))
            return -bigos::ESRCH;
        g_foreground_pgid = pgid;
        return 0;
    }

    void invalidate_foreground_pgid(uint32_t pgid) noexcept {
        if (pgid != 0 && g_foreground_pgid == pgid && !bigos::proc::process_group_has_live_member(pgid)) {
            g_foreground_pgid = 0;
            (void)force_canonical_input_mode();
        }
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

    bool read_raw_char(char *out) noexcept {
        if (take_raw_sequence_byte(out))
            return true;

        TerminalInputRecord record{};
        while (read_input_record(&record)) {
            if (consume_record_as_raw_char(record, out))
                return true;
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

    int read_raw_available_blocking(char *out, size_t capacity, timer::tick_t timeout_ticks) noexcept {
        if (out == nullptr || capacity == 0)
            return sched::WAIT_INVALID;

        while (true) {
            size_t count = 0;
            while (count < capacity && read_raw_char(&out[count]))
                ++count;
            if (count != 0)
                return (int)count;

            const int wait = sched::wait_queue_wait_until(&g_input_wait, &input_available, nullptr, timeout_ticks);
            if (wait < 0)
                return wait;
        }
    }

    TTYInputStats input_stats() noexcept {
        return {g_input.dropped};
    }
}   // namespace bigos::terminal
