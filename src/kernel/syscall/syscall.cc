#include <bigos/syscall.h>

#include <bigos/io.h>
#include <bigos/fs/vfs.h>
#include <bigos/sched.h>
#include <bigos/timer.h>
#include <irq/interrupt.h>
#ifdef BIGOS_USER_PROCESS
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

#ifdef BIGOS_USER_PROCESS
        static int64_t vfs_status_to_syscall(bigos::vfs::Status __status) noexcept {
            return (int64_t)__status;
        }

        static bool copy_user_path(uint64_t __path, char *__dst, size_t __dst_len) noexcept {
            if (__dst == nullptr || __dst_len < 2)
                return false;
            for (size_t i = 0; i < __dst_len; i++) {
                char ch = 0;
                if (!bigos::proc::copy_current_user_buffer(__path + i, &ch, 1))
                    return false;
                __dst[i] = ch;
                if (ch == 0)
                    return true;
            }
            __dst[__dst_len - 1] = 0;
            return false;
        }

        static int64_t sys_write(uint64_t __fd, uint64_t __buffer, uint64_t __len) noexcept {
            if (__fd != 1 || !bigos::proc::validate_user_buffer(__buffer, __len))
                bigos::proc::fault_current_and_exit(SYS_EFAULT);

            char bounded[SYS_WRITE_MAX_LEN + 1];
            if (!bigos::proc::copy_current_user_buffer(__buffer, bounded, __len))
                bigos::proc::fault_current_and_exit(SYS_EFAULT);
            bounded[__len] = 0;

            serial_puts("BIGOS_USER_WRITE_SYSCALL\n");
            serial_puts(bounded);
            return (int64_t)__len;
        }

        static int64_t sys_open(uint64_t __path, uint64_t __flags) noexcept {
            if (!bigos::sched::can_block())
                return SYS_EWOULDBLOCK;

            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return SYS_EFAULT;

            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }

            bigos::vfs::File *file = nullptr;
            const bigos::vfs::Status open_status = bigos::vfs::open_absolute(path, __flags, &file);
            if (open_status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(open_status);

            const int64_t fd = bigos::proc::install_fd_current(file, false);
            if (fd < 0) {
                bigos::vfs::release(file);
                return fd;
            }
            return fd;
        }

        static int64_t sys_read(uint64_t __fd, uint64_t __buffer, uint64_t __len) noexcept {
            if (!bigos::sched::can_block())
                return SYS_EWOULDBLOCK;
            if (__len == 0)
                return 0;
            if (__len > SYS_IO_MAX_LEN || !bigos::proc::validate_user_io_buffer(__buffer, __len))
                return SYS_EFAULT;

            char bounded[SYS_IO_MAX_LEN];
            size_t bytes_read = 0;
            const bigos::vfs::Status read_status =
                bigos::proc::read_fd_current((uint32_t)__fd, bounded, (size_t)__len, &bytes_read);
            if (read_status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(read_status);
            if (bytes_read != 0 && !bigos::proc::copy_to_current_user_buffer(__buffer, bounded, bytes_read))
                return SYS_EFAULT;
            return (int64_t)bytes_read;
        }

        static int64_t sys_close(uint64_t __fd) noexcept {
            return bigos::proc::close_fd_current((uint32_t)__fd);
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
#ifdef BIGOS_USER_PROCESS
            case SYS_WRITE:
                result = __detail::sys_write(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_EXIT:
                bigos::proc::exit_current((int64_t)__frame->rdi);
            case SYS_WAIT:
                result = bigos::proc::wait_current((uint32_t)__frame->rdi, nullptr);
                break;
            case SYS_OPEN:
                result = __detail::sys_open(__frame->rdi, __frame->rsi);
                break;
            case SYS_READ:
                result = __detail::sys_read(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_CLOSE:
                result = __detail::sys_close(__frame->rdi);
                break;
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
