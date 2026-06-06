#include <bigos/syscall.h>

#include <bigos/io.h>
#include <bigos/timer.h>
#include <irq/interrupt.h>

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
