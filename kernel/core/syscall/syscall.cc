#include <bigos/syscall.h>

#include <bigos/io.h>
#include <bigos/console.h>
#include <bigos/fs/vfs.h>
#include <bigos/sched.h>
#include <bigos/time.h>
#include <bigos/timer.h>
#include <bigos/tty.h>
#include <irq/interrupt.h>
#ifdef BIGOS_USER_PROCESS
#include <bigos/cred.h>
#include <bigos/ipc/pipe.h>
#include <bigos/proc.h>
#include <bigos/signal.h>
#endif

NAMESPACE_BIGOS_BEG
namespace sys {
    namespace __detail {
        // Deterministic markers emitted by the diagnostic syscalls. They make the
        // syscall path observable to the source-level checks and the optional
        // emulator smoke without depending on any user-mode buffer.
        constexpr const char *SYSCALL_WRITE_MARKER = "BIGOS_SYSCALL_WRITE\n";
        constexpr uint64_t CR3_ROOT_MASK = 0x000ffffffffff000ull;

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

        // sys_get_time: return the current wall-clock time in Unix epoch seconds.
        // time::current_unix_time() is a read-only arithmetic snapshot over the
        // monotonic tick, so it is safe in int 0x80 context and does not allocate,
        // block, or touch hardware.
        static int64_t sys_get_time() noexcept {
            return bigos::time::current_unix_time();
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
            // Default console fast path: fd 1/2 with no installed file writes to
            // the visible runtime console while preserving the serial marker path
            // used by headless validation. Once a pipe or redirected file is
            // installed at the descriptor, writes route through the fd table.
            if ((__fd == 1 || __fd == 2) && bigos::proc::file_for_fd_current((uint32_t)__fd) == nullptr) {
                if (!bigos::proc::validate_user_buffer(__buffer, __len))
                    bigos::proc::fault_current_and_exit(-bigos::EFAULT);
                char bounded[SYS_WRITE_MAX_LEN + 1];
                if (__len > SYS_WRITE_MAX_LEN || !bigos::proc::copy_current_user_buffer(__buffer, bounded, __len))
                    bigos::proc::fault_current_and_exit(-bigos::EFAULT);
                bounded[__len] = 0;
                serial_puts("BIGOS_USER_WRITE_SYSCALL\n");
                serial_puts(bounded);
                const uint64_t active_root = bigos::mm::read_cr3();
                const bigos::proc::Process *process = bigos::proc::current_process();
                if (process != nullptr && process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
                    (active_root & CR3_ROOT_MASK) != (process->kernel_address_space_root & CR3_ROOT_MASK))
                    bigos::mm::activate_address_space_root(process->kernel_address_space_root);
                bigos::terminal::console_write(bounded);
                if ((bigos::mm::read_cr3() & CR3_ROOT_MASK) != (active_root & CR3_ROOT_MASK))
                    bigos::mm::activate_address_space_root(active_root);
                return (int64_t)__len;
            }

            // File / pipe write. Blockable context required (pipe write may block,
            // file write may perform synchronous block IO).
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if (__len == 0)
                return 0;
            if (__len > SYS_IO_MAX_LEN || !bigos::proc::validate_user_buffer(__buffer, __len))
                return -bigos::EFAULT;
            char bounded[SYS_IO_MAX_LEN];
            if (!bigos::proc::copy_current_user_buffer(__buffer, bounded, __len))
                return -bigos::EFAULT;
            size_t bytes_written = 0;
            const bigos::vfs::Status status =
                bigos::proc::write_fd_current((uint32_t)__fd, bounded, (size_t)__len, &bytes_written);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            return (int64_t)bytes_written;
        }

        static int64_t sys_open(uint64_t __path, uint64_t __flags, uint64_t __mode) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;

            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;

            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }

            // O_CREAT records the calling process identity as owner; the access
            // decision uses the same identity (see cred::permits / decision 7).
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;

            bigos::vfs::File *file = nullptr;
            const bigos::vfs::Status open_status =
                bigos::vfs::open_absolute(path, __flags, (uint32_t)__mode, uid, gid, &file);
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
                return -bigos::EWOULDBLOCK;
            if (__len == 0)
                return 0;
            if (__len > SYS_IO_MAX_LEN || !bigos::proc::validate_user_io_buffer(__buffer, __len))
                return -bigos::EFAULT;

            // Console stdin fast path: fd 0 with no installed file reads from the
            // TTY keyboard input ring (blocking until at least one byte). This
            // lets the interactive shell read commands without a backing vnode.
            if (__fd == 0 && bigos::proc::file_for_fd_current(0) == nullptr) {
                char ch = 0;
                const int r = bigos::terminal::read_char_blocking(&ch, 0);
                if (r < 0)
                    return -bigos::EIO;
                if (!bigos::proc::copy_to_current_user_buffer(__buffer, &ch, 1))
                    return -bigos::EFAULT;
                return 1;
            }

            char bounded[SYS_IO_MAX_LEN];
            size_t bytes_read = 0;
            const bigos::vfs::Status read_status =
                bigos::proc::read_fd_current((uint32_t)__fd, bounded, (size_t)__len, &bytes_read);
            if (read_status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(read_status);
            if (bytes_read != 0 && !bigos::proc::copy_to_current_user_buffer(__buffer, bounded, bytes_read))
                return -bigos::EFAULT;
            return (int64_t)bytes_read;
        }

        static int64_t sys_close(uint64_t __fd) noexcept {
            return bigos::proc::close_fd_current((uint32_t)__fd);
        }

        static int64_t sys_lseek(uint64_t __fd, uint64_t __offset, uint64_t __whence) noexcept {
            uint64_t new_offset = 0;
            const bigos::vfs::Status status =
                bigos::proc::lseek_fd_current((uint32_t)__fd, (int64_t)__offset, (int)__whence, &new_offset);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            return (int64_t)new_offset;
        }

        static int64_t sys_pipe(uint64_t __out_fds) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            // Two 32-bit fds written back to the user array.
            if (!bigos::proc::validate_user_buffer(__out_fds, sizeof(uint32_t) * 2))
                return -bigos::EFAULT;

            bigos::vfs::File *read_file = nullptr;
            bigos::vfs::File *write_file = nullptr;
            const bigos::vfs::Status status = bigos::ipc::create(&read_file, &write_file);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);

            const int64_t read_fd = bigos::proc::install_fd_current(read_file, false);
            if (read_fd < 0) {
                bigos::vfs::release(read_file);
                bigos::vfs::release(write_file);
                return read_fd;
            }
            const int64_t write_fd = bigos::proc::install_fd_current(write_file, false);
            if (write_fd < 0) {
                bigos::proc::close_fd_current((uint32_t)read_fd);
                bigos::vfs::release(write_file);
                return write_fd;
            }
            uint32_t fds[2] = {(uint32_t)read_fd, (uint32_t)write_fd};
            if (!bigos::proc::copy_to_current_user_buffer(__out_fds, fds, sizeof(fds))) {
                bigos::proc::close_fd_current((uint32_t)read_fd);
                bigos::proc::close_fd_current((uint32_t)write_fd);
                return -bigos::EFAULT;
            }
            return 0;
        }

        static int64_t sys_dup(uint64_t __oldfd) noexcept {
            return bigos::proc::dup_fd_current((uint32_t)__oldfd);
        }

        static int64_t sys_dup2(uint64_t __oldfd, uint64_t __newfd) noexcept {
            return bigos::proc::dup2_fd_current((uint32_t)__oldfd, (uint32_t)__newfd);
        }

        static int64_t sys_fsync(uint64_t __fd) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            const bigos::vfs::Status status = bigos::proc::fsync_fd_current((uint32_t)__fd);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            return 0;
        }

        static int64_t sys_mkdir(uint64_t __path, uint64_t __mode) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            return vfs_status_to_syscall(bigos::vfs::mkdir(path, (uint32_t)__mode, uid, gid));
        }

        static int64_t sys_unlink(uint64_t __path) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            return vfs_status_to_syscall(bigos::vfs::unlink(path, uid, gid));
        }

        static int64_t sys_brk(uint64_t __new_break) noexcept {
            return bigos::proc::brk_current(__new_break);
        }

        static int64_t sys_map_anon(uint64_t __len, uint64_t __permissions, uint64_t __flags) noexcept {
            return bigos::proc::map_anonymous_current(__len, __permissions, __flags);
        }

        // Read-only identity queries. Each returns a current-process field via
        // rax. They never allocate, block, or send an EOI. With no current
        // process they return a deterministic -bigos::ESRCH-style error; the
        // current process is always present on the int 0x80 path, but guarding it
        // keeps the helpers side-effect free.
        static int64_t sys_getpid() noexcept {
            const bigos::proc::Process *process = bigos::proc::current_process();
            return process != nullptr ? (int64_t)process->pid : -bigos::EINVAL;
        }

        static int64_t sys_getppid() noexcept {
            const bigos::proc::Process *process = bigos::proc::current_process();
            return process != nullptr ? (int64_t)process->parent_pid : -bigos::EINVAL;
        }

        static int64_t sys_getuid() noexcept {
            const bigos::proc::Process *process = bigos::proc::current_process();
            return process != nullptr ? (int64_t)process->uid : -bigos::EINVAL;
        }

        static int64_t sys_getgid() noexcept {
            const bigos::proc::Process *process = bigos::proc::current_process();
            return process != nullptr ? (int64_t)process->gid : -bigos::EINVAL;
        }

        // sys_kill: deliver signo to the target pid after enforcing the
        // cred::may_signal decision (the single permission enforcement point).
        // Target lookup precedes the permission check: an absent target is
        // -ESRCH, a denied actor is -EPERM (target pending untouched), and an
        // out-of-range signal is -EINVAL. No allocation, no EOI.
        static int64_t sys_kill(uint64_t __pid, uint64_t __signo) noexcept {
            bigos::proc::Process *actor = bigos::proc::current_process();
            if (actor == nullptr)
                return -bigos::ESRCH;
            bigos::proc::Process *target = bigos::proc::find_process((uint32_t)__pid);
            if (target == nullptr)
                return -bigos::ESRCH;
            if (!bigos::cred::may_signal(actor, target))
                return -bigos::EPERM;
            return bigos::signal::kill(target, (int)__signo);
        }

        // sys_sigaction: update the current process disposition for signo. The
        // user passes raw disposition values in registers to avoid a user-buffer
        // struct ABI this stage: rsi = action selector, rdx = handler entry. When
        // old_disp_out (r10) is non-null the previous (action, handler) pair is
        // written back as two uint64 words.
        static int64_t sys_sigaction(
            uint64_t __signo, uint64_t __action, uint64_t __handler, uint64_t __old_out) noexcept {
            bigos::proc::Process *process = bigos::proc::current_process();
            if (process == nullptr)
                return -bigos::EINVAL;
            if (__action > (uint64_t)bigos::signal::SigAction::Handler)
                return -bigos::EINVAL;

            bigos::signal::SigDisposition new_disp;
            new_disp.action = (bigos::signal::SigAction)__action;
            new_disp.handler = __handler;
            bigos::signal::SigDisposition old_disp;
            const int64_t result = bigos::signal::set_disposition(process, (int)__signo, &new_disp, &old_disp);
            if (result != 0)
                return result;
            if (__old_out != 0) {
                uint64_t words[2] = {(uint64_t)old_disp.action, old_disp.handler};
                if (!bigos::proc::copy_to_current_user_buffer(__old_out, words, sizeof(words)))
                    return -bigos::EFAULT;
            }
            return 0;
        }

        // sys_sigprocmask: apply how/set to the current process blocked mask. The
        // old mask is written back through old_out (rdx) when non-null.
        static int64_t sys_sigprocmask(uint64_t __how, uint64_t __set, uint64_t __old_out) noexcept {
            bigos::proc::Process *process = bigos::proc::current_process();
            if (process == nullptr)
                return -bigos::EINVAL;
            bigos::signal::SigSet old_mask = 0;
            const int64_t result = bigos::signal::set_mask(process, __how, __set, &old_mask);
            if (result != 0)
                return result;
            if (__old_out != 0) {
                if (!bigos::proc::copy_to_current_user_buffer(__old_out, &old_mask, sizeof(old_mask)))
                    return -bigos::EFAULT;
            }
            return 0;
        }

        static int64_t sys_wait(uint64_t __pid, uint64_t __status_out) noexcept {
            if (__status_out != 0 && !bigos::proc::validate_user_io_buffer(__status_out, sizeof(int)))
                return -bigos::EFAULT;

            int64_t kernel_status = 0;
            const int64_t waited_pid = bigos::proc::wait_current(
                (uint32_t)__pid, __status_out != 0 ? &kernel_status : nullptr);
            if (waited_pid < 0)
                return waited_pid;

            if (__status_out != 0) {
                const int user_status = (int)kernel_status;
                if (!bigos::proc::copy_to_current_user_buffer(__status_out, &user_status, sizeof(user_status)))
                    return -bigos::EFAULT;
            }
            return waited_pid;
        }

        // Copies a NULL-terminated user pointer array (argv/envp) plus each string
        // into the kernel-side storage backing an ExecArgs. The pointer array is
        // bounded by __max_count and each string by EXEC_MAX_STRING_BYTES; the
        // copied strings live in __string_pool (sized EXEC_MAX_STRING_BYTES *
        // __max_count) and the resulting kernel char* are written into __out.
        // Returns the element count on success, or a negative errno (-EFAULT on a
        // bad user pointer, -E2BIG when the bounds are exceeded).
        static int64_t copy_user_string_vector(uint64_t __user_array, char **__out, char *__string_pool,
            uint32_t __max_count) noexcept {
            if (__user_array == 0) {
                return 0;
            }
            uint32_t count = 0;
            uint64_t pool_used = 0;
            const uint64_t pool_size = (uint64_t)bigos::proc::EXEC_MAX_STRING_BYTES * __max_count;
            for (;;) {
                if (count >= __max_count)
                    return -bigos::E2BIG;
                uint64_t user_ptr = 0;
                if (!bigos::proc::copy_current_user_buffer(
                        __user_array + (uint64_t)count * sizeof(uint64_t), &user_ptr, sizeof(user_ptr)))
                    return -bigos::EFAULT;
                if (user_ptr == 0)
                    break;
                char *dst = __string_pool + pool_used;
                uint64_t len = 0;
                for (;;) {
                    if (len >= bigos::proc::EXEC_MAX_STRING_BYTES || pool_used + len + 1 > pool_size)
                        return -bigos::E2BIG;
                    char ch = 0;
                    if (!bigos::proc::copy_current_user_buffer(user_ptr + len, &ch, 1))
                        return -bigos::EFAULT;
                    dst[len] = ch;
                    if (ch == 0)
                        break;
                    len++;
                }
                pool_used += len + 1;
                __out[count] = dst;
                count++;
            }
            return (int64_t)count;
        }

        // sys_execve: copy the user path/argv/envp into bounded kernel storage,
        // then hand off to proc::execve_current which reads the ELF and replaces
        // the current image (does not return on success). All user-buffer access
        // goes through VMA-backed validation; failures are deterministic negative
        // errno values with the caller left able to continue.
        static int64_t sys_execve(uint64_t __path, uint64_t __argv, uint64_t __envp) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;

            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;

            char *argv[bigos::proc::EXEC_MAX_ARGC + 1] = {};
            char *envp[bigos::proc::EXEC_MAX_ENVC + 1] = {};
            static char argv_pool[bigos::proc::EXEC_MAX_STRING_BYTES * bigos::proc::EXEC_MAX_ARGC];
            static char envp_pool[bigos::proc::EXEC_MAX_STRING_BYTES * bigos::proc::EXEC_MAX_ENVC];

            const int64_t argc = copy_user_string_vector(__argv, argv, argv_pool, bigos::proc::EXEC_MAX_ARGC);
            if (argc < 0)
                return argc;
            const int64_t envc = copy_user_string_vector(__envp, envp, envp_pool, bigos::proc::EXEC_MAX_ENVC);
            if (envc < 0)
                return envc;
            argv[argc] = nullptr;
            envp[envc] = nullptr;

            bigos::proc::ExecArgs args = {argv, (uint32_t)argc, envp, (uint32_t)envc};
            return bigos::proc::execve_current(path, &args);
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
            case SYS_GET_TIME:
                result = __detail::sys_get_time();
                break;
#ifdef BIGOS_USER_PROCESS
            case SYS_WRITE:
                result = __detail::sys_write(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_EXIT:
                bigos::proc::exit_current((int64_t)__frame->rdi);
            case SYS_WAIT:
                result = __detail::sys_wait(__frame->rdi, __frame->rsi);
                break;
            case SYS_OPEN:
                result = __detail::sys_open(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_READ:
                result = __detail::sys_read(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_CLOSE:
                result = __detail::sys_close(__frame->rdi);
                break;
            case SYS_LSEEK:
                result = __detail::sys_lseek(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_PIPE:
                result = __detail::sys_pipe(__frame->rdi);
                break;
            case SYS_DUP:
                result = __detail::sys_dup(__frame->rdi);
                break;
            case SYS_DUP2:
                result = __detail::sys_dup2(__frame->rdi, __frame->rsi);
                break;
            case SYS_FSYNC:
                result = __detail::sys_fsync(__frame->rdi);
                break;
            case SYS_MKDIR:
                result = __detail::sys_mkdir(__frame->rdi, __frame->rsi);
                break;
            case SYS_UNLINK:
                result = __detail::sys_unlink(__frame->rdi);
                break;
            case SYS_EXECVE:
                // execve replaces the current process image and enters the new
                // program on success (does not return). On failure it returns a
                // deterministic negative errno written back below. This software
                // interrupt path sends no i8259 EOI and relaxes no gate/register
                // convention.
                result = __detail::sys_execve(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_BRK:
                result = __detail::sys_brk(__frame->rdi);
                break;
            case SYS_MAP_ANON:
                result = __detail::sys_map_anon(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_FORK:
                // fork duplicates the current process. The dispatcher passes the
                // parent's saved frame so the child can resume from the same int
                // 0x80 return point with rax = 0; the parent receives the child
                // PID (or a negative errno) written back below. This software
                // interrupt path sends no i8259 EOI and does not relax any gate or
                // register convention.
                result = bigos::proc::fork_current(__frame);
                break;
            case SYS_GETPID:
                result = __detail::sys_getpid();
                break;
            case SYS_GETPPID:
                result = __detail::sys_getppid();
                break;
            case SYS_GETUID:
                result = __detail::sys_getuid();
                break;
            case SYS_GETGID:
                result = __detail::sys_getgid();
                break;
            case SYS_KILL:
                result = __detail::sys_kill(__frame->rdi, __frame->rsi);
                break;
            case SYS_SIGACTION:
                result = __detail::sys_sigaction(__frame->rdi, __frame->rsi, __frame->rdx, __frame->r10);
                break;
            case SYS_SIGPROCMASK:
                result = __detail::sys_sigprocmask(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_SIGRETURN:
                // sigreturn restores the interrupted user context (including rax)
                // directly into the frame and must NOT have rax overwritten by the
                // shared return-value path below. It sends no i8259 EOI.
                bigos::signal::sigreturn(__frame);
                return;
#endif
            default:
                // Unknown number: deterministic negative error code, no crash, no
                // exception path.
                result = -bigos::ENOSYS;
                break;
        }

        __frame->rax = (uint64_t)result;
    }
}   // namespace sys
NAMESPACE_BIGOS_END
