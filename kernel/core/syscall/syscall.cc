#include <bigos/syscall.h>

#include <bigos/io.h>
#include <bigos/console.h>
#include <bigos/fs/bigfs.h>
#include <bigos/fs/vfs.h>
#include <bigos/sched.h>
#include <bigos/time.h>
#include <bigos/timer.h>
#include <bigos/tty.h>
#include <irq/interrupt.h>
#include <string.h>
#ifdef BIGOS_USER_PROCESS
#include <bigos/arch_vm_user_boundary.h>
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

        static bool sleep_ms_to_ticks(uint64_t __milliseconds, timer::tick_t *__out_ticks) noexcept {
            if (__out_ticks == nullptr)
                return false;
            if (__milliseconds == 0) {
                *__out_ticks = 0;
                return true;
            }

            constexpr uint64_t MAX_U64 = ~(uint64_t)0;
            constexpr uint64_t HZ = bigos::timer::TIMER_HZ;
            constexpr uint64_t ROUND_UP_BIAS = 999;
            if (__milliseconds > (MAX_U64 - ROUND_UP_BIAS) / HZ)
                return false;

            const uint64_t ticks = (__milliseconds * HZ + ROUND_UP_BIAS) / 1000;
            if (ticks == 0)
                return false;
            const uint64_t now = bigos::timer::ticks();
            if (ticks > MAX_U64 - now)
                return false;

            *__out_ticks = ticks;
            return true;
        }

        static int64_t sys_sleep_ms(uint64_t __milliseconds) noexcept {
            timer::tick_t ticks = 0;
            if (!sleep_ms_to_ticks(__milliseconds, &ticks))
                return -bigos::EINVAL;
            if (ticks == 0)
                return 0;
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;

            const int sleep_result = bigos::timer::sleep_for(ticks);
            if (sleep_result == bigos::sched::WAIT_TIMEOUT || sleep_result == bigos::sched::WAIT_OK)
                return 0;
            if (sleep_result == bigos::sched::WAIT_BLOCK_FORBIDDEN)
                return -bigos::EWOULDBLOCK;
            return -bigos::EIO;
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

        static bool relative_lookup_blocked_by_deleted_cwd(const char *__path) noexcept {
            return __path != nullptr && __path[0] != '/' && bigos::proc::current_cwd_deleted();
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
                const uint64_t active_root = bigos::arch::vm_user::active_address_space_root();
                const bigos::proc::Process *process = bigos::proc::current_process();
                if (process != nullptr && process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
                    !bigos::arch::vm_user::is_active_address_space(process->kernel_address_space_root))
                    bigos::arch::vm_user::activate_kernel_address_space(process->kernel_address_space_root);
                (void)bigos::terminal::default_terminal_write(bounded);
                if (!bigos::arch::vm_user::is_active_address_space(active_root))
                    bigos::arch::vm_user::activate_address_space(active_root);
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
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;

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
                bigos::vfs::open(path, bigos::proc::current_cwd(), __flags, (uint32_t)__mode, uid, gid, &file);
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
                char bounded[SYS_IO_MAX_LEN];
                const bool raw_mode = bigos::terminal::input_mode() == bigos::terminal::TerminalInputMode::Raw;
                const int r = raw_mode
                                  ? bigos::terminal::read_raw_available_blocking(bounded, (size_t)__len, 0)
                                  : bigos::terminal::read_char_blocking(bounded, 0);
                if (r < 0)
                    return -bigos::EIO;
                if (r == 0)
                    return 0;
                if (!bigos::proc::copy_to_current_user_buffer(__buffer, bounded, (size_t)r))
                    return -bigos::EFAULT;
                return r;
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
            if (!bigos::proc::validate_user_io_buffer(__out_fds, sizeof(uint32_t) * 2))
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

#ifdef BIGOS_USER_PROCESS
        static uint64_t *iret_tail(irq::InterruptFrame *__frame) noexcept {
            return (uint64_t *)((uint8_t *)__frame + sizeof(irq::InterruptFrame));
        }

        static void load_user_stack_tail(irq::InterruptFrame *__frame) noexcept {
            if (__frame == nullptr || !bigos::arch::vm_user::is_user_return_frame(__frame))
                return;
            uint64_t *tail = iret_tail(__frame);
            __frame->rsp = tail[0];
            __frame->ss = tail[1];
        }

        static void store_user_stack_tail(irq::InterruptFrame *__frame) noexcept {
            if (__frame == nullptr || !bigos::arch::vm_user::is_user_return_frame(__frame))
                return;
            uint64_t *tail = iret_tail(__frame);
            tail[0] = __frame->rsp;
            tail[1] = __frame->ss;
        }
#endif

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

        static int64_t sys_sync() noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            const bigos::vfs::Status status = bigos::vfs::sync_writable_backend();
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            return 0;
        }

        static int64_t sys_ftruncate(uint64_t __fd, uint64_t __length) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            const bigos::vfs::Status status = bigos::proc::truncate_fd_current((uint32_t)__fd, __length);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            return 0;
        }

        static int64_t sys_truncate(uint64_t __path, uint64_t __length) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            bigos::vfs::File *file = nullptr;
            const bigos::vfs::Status open_status =
                bigos::vfs::open(path, bigos::proc::current_cwd(), bigos::vfs::OPEN_WRONLY, 0, uid, gid, &file);
            if (open_status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(open_status);
            const bigos::vfs::Status trunc_status = bigos::vfs::truncate(file, __length);
            bigos::vfs::release(file);
            return vfs_status_to_syscall(trunc_status);
        }

        static bool access_permitted(const bigos::Metadata &__metadata, uint32_t __uid, uint32_t __gid, uint64_t __mode,
            bigos::cred::Access __access, uint64_t __bit) noexcept {
            return (__mode & __bit) == 0 ||
                   bigos::cred::permits(__metadata.uid, __metadata.gid, __metadata.mode, __uid, __gid, __access);
        }

        static int64_t sys_access(uint64_t __path, uint64_t __mode) noexcept {
            constexpr uint64_t ACCESS_X_OK = 1;
            constexpr uint64_t ACCESS_W_OK = 2;
            constexpr uint64_t ACCESS_R_OK = 4;
            constexpr uint64_t ACCESS_SUPPORTED = ACCESS_X_OK | ACCESS_W_OK | ACCESS_R_OK;
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if ((__mode & ~ACCESS_SUPPORTED) != 0)
                return -bigos::EINVAL;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            char resolved[SYS_PATH_MAX_LEN + 1];
            const bigos::vfs::Status resolve_status =
                bigos::vfs::resolve_path(path, bigos::proc::current_cwd(), resolved, sizeof(resolved));
            if (resolve_status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(resolve_status);
            bigos::Metadata metadata = {};
            const bigos::vfs::Status stat_status = bigos::vfs::stat_absolute(resolved, &metadata);
            if (stat_status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(stat_status);
            if ((__mode & ACCESS_W_OK) != 0 && !bigos::bigfs::owns_path(resolved))
                return -bigos::EACCES;
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            if (!access_permitted(metadata, uid, gid, __mode, bigos::cred::Access::Read, ACCESS_R_OK) ||
                !access_permitted(metadata, uid, gid, __mode, bigos::cred::Access::Write, ACCESS_W_OK) ||
                !access_permitted(metadata, uid, gid, __mode, bigos::cred::Access::Execute, ACCESS_X_OK))
                return -bigos::EACCES;
            return 0;
        }

        static int64_t sys_mkdir(uint64_t __path, uint64_t __mode) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            return vfs_status_to_syscall(
                bigos::vfs::mkdir(path, bigos::proc::current_cwd(), (uint32_t)__mode, uid, gid));
        }

        static int64_t sys_unlink(uint64_t __path) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            return vfs_status_to_syscall(bigos::vfs::unlink(path, bigos::proc::current_cwd(), uid, gid));
        }

        static int64_t sys_rmdir(uint64_t __path) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            char resolved[SYS_PATH_MAX_LEN + 1];
            bigos::vfs::Status status =
                bigos::vfs::resolve_path(path, bigos::proc::current_cwd(), resolved, sizeof(resolved));
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            status = bigos::vfs::rmdir(resolved, uid, gid);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            bigos::proc::mark_cwd_deleted(resolved);
            return 0;
        }

        static int64_t sys_rename(uint64_t __old_path, uint64_t __new_path) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char old_path[SYS_PATH_MAX_LEN + 1];
            char new_path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__old_path, old_path, sizeof(old_path)) ||
                !copy_user_path(__new_path, new_path, sizeof(new_path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(old_path) || relative_lookup_blocked_by_deleted_cwd(new_path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            return vfs_status_to_syscall(bigos::vfs::rename(old_path, new_path, bigos::proc::current_cwd(), uid, gid));
        }

        static int64_t bigfs_status_to_syscall(bigos::bigfs::Status __status) noexcept {
            switch (__status) {
                case bigos::bigfs::Status::Success:
                    return 0;
                case bigos::bigfs::Status::AccessDenied:
                    return -bigos::EACCES;
                case bigos::bigfs::Status::Unsupported:
                    return -bigos::EOPNOTSUPP;
                case bigos::bigfs::Status::IoError:
                    return -bigos::EIO;
                case bigos::bigfs::Status::NoSpace:
                    return -bigos::ENOSPC;
                case bigos::bigfs::Status::Invalid:
                default:
                    return -bigos::EINVAL;
            }
        }

        static int64_t sys_mkfs_bigfs() noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            return bigfs_status_to_syscall(bigos::bigfs::format_persistent());
        }

        static int64_t sys_utimens(uint64_t __path, uint64_t __atime, uint64_t __mtime, uint64_t __flags) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if ((__flags & ~bigos::vfs::UTIME_SUPPORTED_FLAGS) != 0)
                return -bigos::EINVAL;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }
            bigos::proc::Process *process = bigos::proc::current_process();
            const uint32_t uid = process != nullptr ? process->uid : 0;
            const uint32_t gid = process != nullptr ? process->gid : 0;
            const bigos::vfs::Status status =
                bigos::vfs::utimens(path, bigos::proc::current_cwd(), __atime, __mtime, (uint32_t)__flags, uid, gid);
            return vfs_status_to_syscall(status);
        }

        static int64_t sys_readdir(uint64_t __fd, uint64_t __entries, uint64_t __max_entries) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if (__max_entries == 0)
                return -bigos::EINVAL;
            if (__max_entries > SYS_DIRENT_MAX_ENTRIES)
                return -bigos::ERANGE;
            const uint64_t bytes = __max_entries * sizeof(bigos::vfs::DirectoryEntry);
            if (__max_entries > UINT64_MAX / sizeof(bigos::vfs::DirectoryEntry) ||
                !bigos::proc::validate_user_io_buffer(__entries, bytes))
                return -bigos::EFAULT;
            bigos::vfs::DirectoryEntry bounded[SYS_DIRENT_MAX_ENTRIES];
            size_t entries_read = 0;
            const bigos::vfs::Status status =
                bigos::proc::readdir_fd_current((uint32_t)__fd, bounded, (size_t)__max_entries, &entries_read);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            if (entries_read != 0 && !bigos::proc::copy_to_current_user_buffer(
                                         __entries, bounded, entries_read * sizeof(bigos::vfs::DirectoryEntry)))
                return -bigos::EFAULT;
            return (int64_t)entries_read;
        }

        static int64_t sys_stat(uint64_t __path, uint64_t __out) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if (!bigos::proc::validate_user_io_buffer(__out, sizeof(bigos::Metadata)))
                return -bigos::EFAULT;

            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;
            if (!bigos::vfs::initialized()) {
                const bigos::vfs::Status init_status = bigos::vfs::init();
                if (init_status != bigos::vfs::Status::Success)
                    return vfs_status_to_syscall(init_status);
            }

            bigos::Metadata metadata = {};
            const bigos::vfs::Status status = bigos::vfs::stat_path(path, bigos::proc::current_cwd(), &metadata);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            if (!bigos::proc::copy_to_current_user_buffer(__out, &metadata, sizeof(metadata)))
                return -bigos::EFAULT;
            return 0;
        }

        static int64_t sys_fstat(uint64_t __fd, uint64_t __out) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            if (!bigos::proc::validate_user_io_buffer(__out, sizeof(bigos::Metadata)))
                return -bigos::EFAULT;
            bigos::Metadata metadata = {};
            const bigos::vfs::Status status = bigos::proc::stat_fd_current((uint32_t)__fd, &metadata);
            if (status != bigos::vfs::Status::Success)
                return vfs_status_to_syscall(status);
            if (!bigos::proc::copy_to_current_user_buffer(__out, &metadata, sizeof(metadata)))
                return -bigos::EFAULT;
            return 0;
        }

        static int64_t sys_chdir(uint64_t __path) noexcept {
            if (!bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            char path[SYS_PATH_MAX_LEN + 1];
            if (!copy_user_path(__path, path, sizeof(path)))
                return -bigos::EFAULT;
            return bigos::proc::chdir_current(path);
        }

        static int64_t sys_getcwd(uint64_t __buffer, uint64_t __len) noexcept {
            if (__len == 0)
                return -bigos::EINVAL;
            if (!bigos::proc::validate_user_io_buffer(__buffer, __len))
                return -bigos::EFAULT;
            char cwd[SYS_PATH_MAX_LEN + 1];
            const int64_t status = bigos::proc::getcwd_current(cwd, sizeof(cwd));
            if (status < 0)
                return status;
            const size_t len = strlen(cwd) + 1;
            if (__len < len)
                return -bigos::ERANGE;
            if (!bigos::proc::copy_to_current_user_buffer(__buffer, cwd, len))
                return -bigos::EFAULT;
            return 0;
        }

        static int64_t sys_brk(uint64_t __new_break) noexcept {
            return bigos::proc::brk_current(__new_break);
        }

        static int64_t sys_map_anon(uint64_t __len, uint64_t __permissions, uint64_t __flags) noexcept {
            return bigos::proc::map_anonymous_current(__len, __permissions, __flags);
        }

        static int64_t sys_unmap_anon(uint64_t __addr, uint64_t __len) noexcept {
            return bigos::proc::unmap_anonymous_current(__addr, __len);
        }

        static int64_t sys_protect_anon(uint64_t __addr, uint64_t __len, uint64_t __permissions) noexcept {
            return bigos::proc::protect_anonymous_current(__addr, __len, __permissions);
        }

        static int64_t sys_map_file(
            uint64_t __fd, uint64_t __offset, uint64_t __len, uint64_t __permissions, uint64_t __flags) noexcept {
            return bigos::proc::map_file_current(__fd, __offset, __len, __permissions, __flags);
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

        static int64_t sys_getpgid(uint64_t __pid) noexcept {
            return bigos::proc::getpgid_current((uint32_t)__pid);
        }

        static int64_t sys_getsid(uint64_t __pid) noexcept {
            return bigos::proc::getsid_current((uint32_t)__pid);
        }

        static int64_t sys_setpgid(uint64_t __pid, uint64_t __pgid) noexcept {
            return bigos::proc::setpgid_current((uint32_t)__pid, (uint32_t)__pgid);
        }

        static int64_t sys_setsid() noexcept {
            return bigos::proc::setsid_current();
        }

        static int64_t sys_tcgetpgrp() noexcept {
            const uint32_t pgid = bigos::terminal::foreground_pgid();
            return pgid != 0 ? (int64_t)pgid : -bigos::ESRCH;
        }

        static int64_t sys_tcsetpgrp(uint64_t __pgid) noexcept {
            return bigos::terminal::set_foreground_pgid((uint32_t)__pgid);
        }

        static int64_t sys_tcgetmode(uint64_t __out) noexcept {
            if (!bigos::proc::validate_user_io_buffer(__out, sizeof(bigos::terminal::TerminalMode)))
                return -bigos::EFAULT;
            bigos::terminal::TerminalMode mode{};
            const int64_t status = bigos::terminal::get_input_mode(&mode);
            if (status < 0)
                return status;
            if (!bigos::proc::copy_to_current_user_buffer(__out, &mode, sizeof(mode)))
                return -bigos::EFAULT;
            return 0;
        }

        static int64_t sys_tcsetmode(uint64_t __mode) noexcept {
            if (!bigos::proc::validate_user_buffer(__mode, sizeof(bigos::terminal::TerminalMode)))
                return -bigos::EFAULT;
            bigos::terminal::TerminalMode mode{};
            if (!bigos::proc::copy_current_user_buffer(__mode, &mode, sizeof(mode)))
                return -bigos::EFAULT;
            return bigos::terminal::set_input_mode(mode);
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
            const int64_t waited_pid =
                bigos::proc::wait_current((uint32_t)__pid, __status_out != 0 ? &kernel_status : nullptr);
            if (waited_pid < 0)
                return waited_pid;

            if (__status_out != 0) {
                const int user_status = (int)kernel_status;
                if (!bigos::proc::copy_to_current_user_buffer(__status_out, &user_status, sizeof(user_status)))
                    return -bigos::EFAULT;
            }
            return waited_pid;
        }

        static int64_t sys_waitpid(uint64_t __pid, uint64_t __status_out, uint64_t __options) noexcept {
            if (__status_out != 0 && !bigos::proc::validate_user_io_buffer(__status_out, sizeof(int)))
                return -bigos::EFAULT;

            int64_t kernel_status = 0;
            const int64_t waited_pid = bigos::proc::wait_current(
                (uint32_t)__pid, __status_out != 0 ? &kernel_status : nullptr, (uint32_t)__options);
            if (waited_pid <= 0)
                return waited_pid;

            if (__status_out != 0) {
                const int user_status = (int)kernel_status;
                if (!bigos::proc::copy_to_current_user_buffer(__status_out, &user_status, sizeof(user_status)))
                    return -bigos::EFAULT;
            }
            return waited_pid;
        }

        static int64_t sys_fcntl(uint64_t __fd, uint64_t __cmd, uint64_t __arg) noexcept {
            if (__cmd == (uint64_t)bigos::proc::FCNTL_F_DUPFD && !bigos::sched::can_block())
                return -bigos::EWOULDBLOCK;
            return bigos::proc::fcntl_fd_current((uint32_t)__fd, (int)__cmd, __arg);
        }

        // Copies a NULL-terminated user pointer array (argv/envp) plus each string
        // into the kernel-side storage backing an ExecArgs. The pointer array is
        // bounded by __max_count and each string by EXEC_MAX_STRING_BYTES; the
        // copied strings live in __string_pool (sized EXEC_MAX_STRING_BYTES *
        // __max_count) and the resulting kernel char* are written into __out.
        // Returns the element count on success, or a negative errno (-EFAULT on a
        // bad user pointer, -E2BIG when the bounds are exceeded).
        static int64_t copy_user_string_vector(
            uint64_t __user_array, char **__out, char *__string_pool, uint32_t __max_count) noexcept {
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
            if (relative_lookup_blocked_by_deleted_cwd(path))
                return -bigos::ENOENT;

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
            case SYS_SLEEP_MS:
                result = __detail::sys_sleep_ms(__frame->rdi);
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
            case SYS_WAITPID:
                result = __detail::sys_waitpid(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_FCNTL:
                result = __detail::sys_fcntl(__frame->rdi, __frame->rsi, __frame->rdx);
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
            case SYS_SYNC:
                result = __detail::sys_sync();
                break;
            case SYS_MKDIR:
                result = __detail::sys_mkdir(__frame->rdi, __frame->rsi);
                break;
            case SYS_UNLINK:
                result = __detail::sys_unlink(__frame->rdi);
                break;
            case SYS_RMDIR:
                result = __detail::sys_rmdir(__frame->rdi);
                break;
            case SYS_FTRUNCATE:
                result = __detail::sys_ftruncate(__frame->rdi, __frame->rsi);
                break;
            case SYS_TRUNCATE:
                result = __detail::sys_truncate(__frame->rdi, __frame->rsi);
                break;
            case SYS_ACCESS:
                result = __detail::sys_access(__frame->rdi, __frame->rsi);
                break;
            case SYS_RENAME:
                result = __detail::sys_rename(__frame->rdi, __frame->rsi);
                break;
            case SYS_MKFS_BIGFS:
                result = __detail::sys_mkfs_bigfs();
                break;
            case SYS_UTIMENS:
                result = __detail::sys_utimens(__frame->rdi, __frame->rsi, __frame->rdx, __frame->r10);
                break;
            case SYS_EXECVE:
                // execve replaces the current process image and enters the new
                // program on success (does not return). On failure it returns a
                // deterministic negative errno written back below. This software
                // interrupt path sends no i8259 EOI and relaxes no gate/register
                // convention.
                result = __detail::sys_execve(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_READDIR:
                result = __detail::sys_readdir(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_STAT:
                result = __detail::sys_stat(__frame->rdi, __frame->rsi);
                break;
            case SYS_FSTAT:
                result = __detail::sys_fstat(__frame->rdi, __frame->rsi);
                break;
            case SYS_CHDIR:
                result = __detail::sys_chdir(__frame->rdi);
                break;
            case SYS_GETCWD:
                result = __detail::sys_getcwd(__frame->rdi, __frame->rsi);
                break;
            case SYS_BRK:
                result = __detail::sys_brk(__frame->rdi);
                break;
            case SYS_MAP_ANON:
                result = __detail::sys_map_anon(__frame->rdi, __frame->rsi, __frame->rdx);
                break;
            case SYS_MAP_FILE:
                result = __detail::sys_map_file(__frame->rdi, __frame->rsi, __frame->rdx, __frame->r10, __frame->r8);
                break;
            case SYS_UNMAP_ANON:
                result = __detail::sys_unmap_anon(__frame->rdi, __frame->rsi);
                break;
            case SYS_PROTECT_ANON:
                result = __detail::sys_protect_anon(__frame->rdi, __frame->rsi, __frame->rdx);
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
            case SYS_GETPGID:
                result = __detail::sys_getpgid(__frame->rdi);
                break;
            case SYS_GETSID:
                result = __detail::sys_getsid(__frame->rdi);
                break;
            case SYS_SETPGID:
                result = __detail::sys_setpgid(__frame->rdi, __frame->rsi);
                break;
            case SYS_SETSID:
                result = __detail::sys_setsid();
                break;
            case SYS_TCGETPGRP:
                result = __detail::sys_tcgetpgrp();
                break;
            case SYS_TCSETPGRP:
                result = __detail::sys_tcsetpgrp(__frame->rdi);
                break;
            case SYS_TCGETMODE:
                result = __detail::sys_tcgetmode(__frame->rdi);
                break;
            case SYS_TCSETMODE:
                result = __detail::sys_tcsetmode(__frame->rdi);
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
                // shared return-value path below. The real ring3 rsp/ss live in
                // the iret tail, so synchronize it around the restore. It sends no
                // i8259 EOI.
                __detail::load_user_stack_tail(__frame);
                bigos::signal::sigreturn(__frame);
                __detail::store_user_stack_tail(__frame);
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
