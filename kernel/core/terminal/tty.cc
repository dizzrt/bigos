#include <bigos/arch_vm_user_boundary.h>
#include <bigos/console.h>
#include <bigos/errno.h>
#include <bigos/io.h>
#include <bigos/keyboard.h>
#include <bigos/memory.h>
#include <bigos/proc.h>
#include <bigos/sched.h>
#include <bigos/signal.h>
#include <bigos/syscall.h>
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

        struct PendingSequenceState {
            char bytes[4];
            uint8_t len;
            uint8_t index;
        };

        PendingSequenceState g_pending_sequence;

        size_t next_index(size_t index) noexcept {
            return (index + 1) % TTY_INPUT_CAPACITY;
        }

        bool input_available(void *) noexcept {
            return g_pending_sequence.index < g_pending_sequence.len || g_input.tail != g_input.head;
        }

        bool input_record_available(void *) noexcept {
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

        void set_pending_sequence(const char *bytes, uint8_t len) noexcept {
            g_pending_sequence.len = len;
            g_pending_sequence.index = 0;
            for (uint8_t i = 0; i < len && i < sizeof(g_pending_sequence.bytes); ++i)
                g_pending_sequence.bytes[i] = bytes[i];
        }

        bool take_pending_sequence_byte(char *out) noexcept {
            if (out == nullptr || g_pending_sequence.index >= g_pending_sequence.len)
                return false;
            *out = g_pending_sequence.bytes[g_pending_sequence.index++];
            if (g_pending_sequence.index >= g_pending_sequence.len) {
                g_pending_sequence.index = 0;
                g_pending_sequence.len = 0;
            }
            return true;
        }

        bool set_navigation_sequence(TerminalControl control) noexcept {
            switch (control) {
                case TerminalControl::NavigateUp:
                    set_pending_sequence("\x1b[A", 3);
                    return true;
                case TerminalControl::NavigateDown:
                    set_pending_sequence("\x1b[B", 3);
                    return true;
                case TerminalControl::NavigateRight:
                    set_pending_sequence("\x1b[C", 3);
                    return true;
                case TerminalControl::NavigateLeft:
                    set_pending_sequence("\x1b[D", 3);
                    return true;
                case TerminalControl::NavigateHome:
                    set_pending_sequence("\x1b[H", 3);
                    return true;
                case TerminalControl::NavigateEnd:
                    set_pending_sequence("\x1b[F", 3);
                    return true;
                case TerminalControl::NavigateDelete:
                    set_pending_sequence("\x1b[3~", 4);
                    return true;
                case TerminalControl::NavigatePageUp:
                    set_pending_sequence("\x1b[5~", 4);
                    return true;
                case TerminalControl::NavigatePageDown:
                    set_pending_sequence("\x1b[6~", 4);
                    return true;
                default:
                    return false;
            }
        }

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
                case TerminalControl::NavigateUp:
                case TerminalControl::NavigateDown:
                case TerminalControl::NavigateRight:
                case TerminalControl::NavigateLeft:
                case TerminalControl::NavigateHome:
                case TerminalControl::NavigateEnd:
                case TerminalControl::NavigateDelete:
                case TerminalControl::NavigatePageUp:
                case TerminalControl::NavigatePageDown:
                    if (set_navigation_sequence(record.control) && take_pending_sequence_byte(out))
                        return ConsumeResult::Character;
                    return ConsumeResult::Ignored;
                case TerminalControl::KernelScrollPageUp:
                    console_scroll_page_up();
                    return ConsumeResult::Ignored;
                case TerminalControl::KernelScrollPageDown:
                    console_scroll_page_down();
                    return ConsumeResult::Ignored;
                case TerminalControl::Unsupported:
                    return ConsumeResult::Ignored;
                case TerminalControl::None:
                    break;
            }

            *out = record.ch;
            return ConsumeResult::Character;
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
                case TerminalControl::NavigateUp:
                case TerminalControl::NavigateDown:
                case TerminalControl::NavigateRight:
                case TerminalControl::NavigateLeft:
                case TerminalControl::NavigateHome:
                case TerminalControl::NavigateEnd:
                case TerminalControl::NavigateDelete:
                case TerminalControl::NavigatePageUp:
                case TerminalControl::NavigatePageDown:
                    return set_navigation_sequence(record.control) && take_pending_sequence_byte(out);
                case TerminalControl::KernelScrollPageUp:
                case TerminalControl::KernelScrollPageDown:
                    return false;
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
        g_pending_sequence = {};
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
        g_pending_sequence = {};
        return 0;
    }

    int64_t force_canonical_input_mode() noexcept {
        g_input_mode = TerminalInputMode::Canonical;
        g_pending_sequence = {};
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
        if (take_pending_sequence_byte(out))
            return true;

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
        if (take_pending_sequence_byte(out))
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

            const int wait =
                sched::wait_queue_wait_until(&g_input_wait, &input_record_available, nullptr, timeout_ticks);
            if (wait < 0)
                return wait;
        }
    }

    int read_char_blocking(char *out, timer::tick_t timeout_ticks) noexcept {
        if (out == nullptr)
            return sched::WAIT_INVALID;

        while (true) {
            if (take_pending_sequence_byte(out))
                return 1;

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

    namespace {
        // Handle layer over the global terminal device. The vfs::File read/write
        // ops dispatch here; the device-layer global state (g_input/g_input_wait)
        // is shared by every handle, so close is a no-op and create/release only
        // manage the File structure itself.
        vfs::Status tty_read(vfs::File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
            if (__bytes_read != nullptr)
                *__bytes_read = 0;
            if (__file == nullptr)
                return vfs::Status::BadFileDescriptor;
            if (__len == 0)
                return vfs::Status::Success;
            if (__dst == nullptr)
                return vfs::Status::InvalidArgument;
            if (!sched::can_block())
                return vfs::Status::WouldBlock;

            char *out = (char *)__dst;
            const bool raw_mode = input_mode() == TerminalInputMode::Raw;
            const int r = raw_mode ? read_raw_available_blocking(out, __len, 0) : read_char_blocking(out, 0);
            if (r < 0)
                return vfs::Status::BlockError;
            if (__bytes_read != nullptr)
                *__bytes_read = (size_t)r;
            return vfs::Status::Success;
        }

        // Terminal write. Byte-for-byte equivalent to the former sys_write fd 1/2
        // branch: emit the BIGOS_USER_WRITE_SYSCALL marker plus content on COM1,
        // then write the bounded content to the default console with the same
        // user/kernel address-space switch and restore as before. SYS_WRITE_MAX_LEN
        // still bounds the bytes consumed in a single call.
        vfs::Status tty_write(vfs::File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept {
            if (__bytes_written != nullptr)
                *__bytes_written = 0;
            if (__file == nullptr)
                return vfs::Status::BadFileDescriptor;
            if (__len == 0)
                return vfs::Status::Success;
            if (__src == nullptr)
                return vfs::Status::InvalidArgument;
            if (__len > bigos::sys::SYS_WRITE_MAX_LEN)
                return vfs::Status::InvalidArgument;

            char bounded[bigos::sys::SYS_WRITE_MAX_LEN + 1];
            const char *in = (const char *)__src;
            for (size_t i = 0; i < __len; ++i)
                bounded[i] = in[i];
            bounded[__len] = 0;
            serial_puts("BIGOS_USER_WRITE_SYSCALL\n");
            serial_puts(bounded);
            const uint64_t active_root = bigos::arch::vm_user::active_address_space_root();
            const bigos::proc::Process *process = bigos::proc::current_process();
            if (process != nullptr && process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
                !bigos::arch::vm_user::is_active_address_space(process->kernel_address_space_root))
                bigos::arch::vm_user::activate_kernel_address_space(process->kernel_address_space_root);
            (void)default_terminal_write(bounded);
            if (!bigos::arch::vm_user::is_active_address_space(active_root))
                bigos::arch::vm_user::activate_address_space(active_root);
            if (__bytes_written != nullptr)
                *__bytes_written = __len;
            return vfs::Status::Success;
        }

        vfs::Status tty_lseek(vfs::File *, int64_t, int, uint64_t *) noexcept {
            return vfs::Status::NotSeekable;
        }

        // Device-layer no-op: a terminal handle owns no device state, so closing it
        // must not touch the global input ring or wait queue. The File structure
        // itself is freed by vfs::release.
        void tty_close(vfs::File *) noexcept {}

        // read/close occupy the fixed first two ops slots; write/lseek follow. A
        // poll slot can be appended later (M13.1 readiness) without reordering.
        const vfs::FileOperations TTY_OPS = {&tty_read, &tty_close, &tty_write, &tty_lseek, nullptr, nullptr};
    }   // namespace

    vfs::File *create_tty_file() noexcept {
        auto *file = (vfs::File *)bigos::kmalloc(sizeof(vfs::File));
        if (file == nullptr)
            return nullptr;
        file->ops = &TTY_OPS;
        file->vnode = nullptr;
        file->offset = 0;
        file->ref_count = 1;
        file->readable = true;
        file->close_on_exec = false;
        file->private_data = &g_input;
        file->writable = true;
        file->identity = {};
        return file;
    }

    bool is_tty_file(const vfs::File *__file) noexcept {
        return __file != nullptr && __file->ops == &TTY_OPS;
    }
}   // namespace bigos::terminal
