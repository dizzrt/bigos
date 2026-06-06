#include <bigos/syscall.h>

#include <bigos/io.h>
#include <bigos/timer.h>
#include <irq/interrupt.h>
#ifdef BIGOS_USER_PROGRAM_SMOKE
#include <bigos/proc.h>
#endif

NAMESPACE_BIGOS_BEG
namespace sys {
    namespace __detail {
        // Deterministic markers emitted by the diagnostic syscalls. They make the
        // syscall path observable to the source-level checks and the optional
        // emulator smoke without depending on any user-mode buffer.
        constexpr const char *SYSCALL_WRITE_MARKER = "BIGOS_SYSCALL_WRITE\n";

        // sys_debug_write: emit a kernel-internal bounded buffer through the
        // existing serial/console path. This stage does NOT validate any pointer
        // because the only caller is ring0 kernel code; the buffer is a fixed,
        // kernel-internal, bounded source.
        //
        // ring3 prerequisite: once user mode can trigger this syscall, the buffer
        // pointer and length supplied by user space MUST be validated (range check
        // against the user address space and a bounded copy) before any output.
        static int64_t sys_debug_write() noexcept {
            // Bounded kernel-internal source; not derived from any caller pointer.
            const char *const kernel_buffer = SYSCALL_WRITE_MARKER;
            serial_puts(kernel_buffer);
            kputs(kernel_buffer);

            // Return the number of bytes emitted so the return-value register path
            // is exercised with a deterministic value.
            int64_t written = 0;
            for (const char *p = kernel_buffer; *p != 0; ++p)
                ++written;
            return written;
        }

        // sys_get_tick: return the monotonic kernel tick. timer::ticks() is a
        // context-agnostic bounded read, so it is safe in int 0x80 interrupt
        // context and does not allocate or block.
        static int64_t sys_get_tick() noexcept {
            return (int64_t)bigos::timer::ticks();
        }

#ifdef BIGOS_USER_PROGRAM_SMOKE
        static int64_t sys_write(uint64_t __fd, uint64_t __buffer, uint64_t __len) noexcept {
            if (__fd != 1 || !bigos::proc::validate_user_buffer(__buffer, __len))
                return SYS_EFAULT;

            char bounded[SYS_WRITE_MAX_LEN + 1];
            const char *src = (const char *)__buffer;
            for (uint64_t i = 0; i < __len; i++)
                bounded[i] = src[i];
            bounded[__len] = 0;

            serial_puts("BIGOS_USER_WRITE_SYSCALL\n");
            serial_puts(bounded);
            return (int64_t)__len;
        }
#endif
    }   // namespace __detail

    void dispatch(bigos::irq::InterruptFrame *__frame) noexcept {
        if (__frame == nullptr)
            return;

        // Syscall number is passed in rax (see the ABI in bigos/syscall.h). The
        // result is written back into rax; the caller reads it after iretq.
        const uint64_t number = __frame->rax;

        int64_t result;
        switch (number) {
            case SYS_DEBUG_WRITE:
                result = __detail::sys_debug_write();
                break;
            case SYS_GET_TICK:
                result = __detail::sys_get_tick();
                break;
#ifdef BIGOS_USER_PROGRAM_SMOKE
            case SYS_WRITE:
                result = __detail::sys_write(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_EXIT:
                bigos::proc::exit_current((int64_t)__frame->rdi);
#endif
            default:
                // Unknown number: deterministic negative error code, no crash, no
                // exception path.
                result = SYS_ENOSYS;
                break;
        }

        __frame->rax = (uint64_t)result;
    }
}   // namespace sys
NAMESPACE_BIGOS_END
