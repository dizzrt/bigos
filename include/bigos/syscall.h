#ifndef _BIG_SYSCALL_H
#define _BIG_SYSCALL_H

#include <bigos/types.h>
#include <bigos/errno.h>

namespace bigos::irq {
    struct InterruptFrame;
}   // namespace bigos::irq

namespace bigos::sys {
    // Minimal syscall ABI (int 0x80 software-interrupt entry; ring0 self-tests and
    // the default-off first user program use the same register ABI). The mapping below is fixed by source-level checks
    // and documented in docs/en/arch/syscall-entry.md:
    //
    //   syscall number -> rax           (InterruptFrame.rax on entry)
    //   argument 0     -> rdi           (InterruptFrame.rdi)
    //   argument 1     -> rsi           (InterruptFrame.rsi)
    //   argument 2     -> rdx           (InterruptFrame.rdx)
    //   argument 3     -> r10           (InterruptFrame.r10)
    //   argument 4     -> r8            (InterruptFrame.r8)
    //   argument 5     -> r9            (InterruptFrame.r9)
    //   return value   -> rax           (dispatcher writes InterruptFrame.rax;
    //                                    the caller reads rax after iretq)
    //
    // Registers other than the return value are callee-clobbered by convention;
    // the caller is responsible for saving anything it needs. r10 (not rcx) is the
    // 4th argument register to avoid clashing with rcx, which int 0x80 / iretq
    // semantics make unsuitable as a stable argument slot.

    enum SyscallNumber : uint64_t {
        SYS_DEBUG_WRITE = 0,    // emit a kernel-internal bounded buffer to console/serial
        SYS_GET_TICK = 1,       // return the monotonic kernel tick via rax
        SYS_WRITE = 2,          // fd, user buffer, bounded length -> deterministic write result
        SYS_EXIT = 3,           // exit code -> terminate current user process, does not return
        SYS_WAIT = 4,           // pid or WAIT_ANY, optional int* status -> child pid, or negative wait error
        SYS_OPEN = 5,           // user path, bounded open flags -> process-local fd
        SYS_READ = 6,           // fd, user buffer, bounded length -> deterministic read result
        SYS_CLOSE = 7,          // fd -> close current process descriptor
        SYS_BRK = 8,            // requested break -> committed heap break or deterministic negative error
        SYS_MAP_ANON = 9,       // length, permissions, flags -> restricted anonymous user mapping
        SYS_FORK = 10,          // duplicate current process -> parent gets child PID, child gets 0,
                                // failure returns a negative errno (e.g. -ENOMEM / -EAGAIN)
        SYS_GET_TIME = 11,      // return current wall-clock Unix seconds via rax (read-only)
        SYS_GETPID = 12,        // return current process pid via rax (read-only)
        SYS_GETPPID = 13,       // return current process parent_pid via rax (read-only)
        SYS_GETUID = 14,        // return current process uid via rax (read-only)
        SYS_GETGID = 15,        // return current process gid via rax (read-only)
        SYS_KILL = 16,          // (pid, signo) -> 0 or -ESRCH/-EPERM/-EINVAL; enforces cred::may_signal
        SYS_SIGACTION = 17,     // (signo, new_disp, old_disp_out) -> 0 or -EINVAL
        SYS_SIGPROCMASK = 18,   // (how, new_set, old_set_out) -> 0 or -EINVAL
        SYS_SIGRETURN = 19,     // restore the interrupted user context from the user-stack signal frame
        SYS_LSEEK = 20,         // (fd, offset, whence) -> new offset, or -EBADF/-ESPIPE/-EINVAL
        SYS_PIPE = 21,          // (int out_fds[2]) -> 0, or -EMFILE/-ENOMEM/-EFAULT
        SYS_DUP = 22,           // (oldfd) -> newfd, or -EBADF/-EMFILE
        SYS_DUP2 = 23,          // (oldfd, newfd) -> newfd, or -EBADF/-EMFILE
        SYS_FSYNC = 24,         // (fd) -> 0, or -EBADF/-EIO
        SYS_MKDIR = 25,         // (path, mode) -> 0, or -EEXIST/-EACCES/-ENOSPC/-EROFS/-EINVAL
        SYS_UNLINK = 26,        // (path) -> 0, or -ENOENT/-EACCES/-EISDIR/-EROFS/-EINVAL
        SYS_EXECVE = 27,        // (rdi=path, rsi=argv, rdx=envp) -> replaces the current
                                // process image and enters the new program entry on
                                // success (does not return); on failure returns a
                                // deterministic negative errno (-ENOENT/-EACCES/-ENOEXEC/
                                // -E2BIG/-EFAULT/-ENOMEM). argv/envp are NULL-terminated
                                // user pointer arrays copied through VMA-backed validation
                                // and bounded by EXEC_MAX_ARGC/EXEC_MAX_ENVC/
                                // EXEC_MAX_STRING_BYTES.
        SYS_READDIR = 28,       // (fd, struct bigos_dirent *entries, max_entries) -> entry count,
                                // -EINVAL for illegal arguments, -ERANGE when the
                                // caller asks beyond the bounded batch capacity,
                                // or another negative fd/VFS errno.
        SYS_STAT = 29,          // (path, struct bigos_metadata *out) -> 0 or negative errno.
        SYS_FSTAT = 30,         // (fd, struct bigos_metadata *out) -> 0 or negative errno.
        SYS_CHDIR = 31,         // (path) -> 0 or negative errno; cwd changes only after directory validation.
        SYS_GETCWD = 32,        // (user buffer, len) -> 0 or -ERANGE/-EFAULT/-EINVAL.
        SYS_RENAME = 33,        // (oldpath, newpath) -> 0 or negative errno; bounded /rw regular-file rename.
        SYS_MKFS_BIGFS = 34,    // explicit bounded format of the configured persistent /rw test disk.
        SYS_MAP_FILE = 35,      // (rdi=fd, rsi=offset, rdx=len, r10=permissions, r8=flags) ->
                                // mapped user address of a bounded read-only file-backed
                                // mapping, or a negative errno
                                // (-EBADF/-EACCES/-EINVAL/-ENOMEM/-EWOULDBLOCK). offset and
                                // len are page-aligned; permissions must be read-only and
                                // non-W+X; flags are reserved (must be 0). No partial VMA is
                                // published on failure.
        SYS_UNMAP_ANON = 36,    // (rdi=addr, rsi=len) -> bounded anonymous/private unmap.
                                // Page-aligned, fully covered anonymous ranges only; not full POSIX munmap.
        SYS_PROTECT_ANON = 37,  // (rdi=addr, rsi=len, rdx=permissions) -> bounded anonymous/private
                                // protection change. Rejects W+X and unsupported backing.
        SYS_RMDIR = 38,         // (path) -> remove an empty writable /rw directory, or negative errno.
        SYS_FTRUNCATE = 39,     // (fd, length) -> bounded /rw regular-file truncate, or negative errno.
        SYS_SYNC = 40,          // () -> synchronize active writable backend dirty state, or negative errno.
                                // Bounded BigOS subset: not full POSIX sync(2),
                                // fdatasync, async flush, crash recovery, or
                                // broad mount namespace synchronization.
        SYS_GETPGID = 41,       // (pid or 0=current) -> process group id, or -ESRCH
        SYS_GETSID = 42,        // (pid or 0=current) -> session id, or -ESRCH
        SYS_SETPGID = 43,       // (pid or 0=current, pgid or 0=pid) -> 0 or deterministic errno
        SYS_SETSID = 44,        // () -> new sid/pgid equal to current pid, or -EPERM/-ESRCH
        SYS_TCGETPGRP = 45,     // () -> default terminal foreground pgid, or deterministic errno
        SYS_TCSETPGRP = 46,     // (pgid) -> bind default terminal foreground pgid, or errno
        SYS_WAITPID = 47,       // (pid/WAIT_ANY, optional int* status, options) -> child pid, 0 for WNOHANG miss, or errno
        SYS_FCNTL = 48,         // bounded fd-control: F_GETFD/F_SETFD/F_DUPFD over process-local fd entries
        SYS_ACCESS = 49,        // (path, mode) -> bounded path visibility/permission check through VFS metadata/open rules
        SYS_TRUNCATE = 50,      // (path, length) -> bounded /rw regular-file truncate by path, or negative errno
        SYS_TCGETMODE = 51,     // (struct TerminalMode *out) -> default terminal canonical/raw mode snapshot
        SYS_TCSETMODE = 52,     // (const struct TerminalMode *mode) -> set default terminal mode with foreground checks
        SYS_SLEEP_MS = 53,      // (milliseconds) -> 0 after coarse tick-based blocking sleep, or errno.
                                // Bounded BigOS sleep: not nanosleep, alarm,
                                // timerfd, high-resolution timer, or
                                // signal-interruptible POSIX sleep.
        SYS_UTIMENS = 54,       // (path, atime, mtime, flags) -> bounded second-resolution timestamp update.
        SYS_SOCKET = 55,        // (domain, type, protocol) -> bounded UDP socket fd, or negative errno.
                                // Only the bounded BigOS UDP subset is accepted
                                // (domain=SOCKET_AF_INET, type=SOCKET_SOCK_DGRAM,
                                // protocol=0/SOCKET_IPPROTO_UDP); anything else is
                                // rejected with a deterministic -EINVAL. Not a full
                                // POSIX socket(2): no TCP/stream, AF_* family matrix,
                                // connect/listen/accept, or option matrix.
        SYS_BIND = 56,          // (fd, const struct SockAddrIn*, addrlen) -> 0 or negative errno.
                                // Binds a UDP socket fd to a local port through the
                                // kernel-internal bigos::net UDP API. addrlen MUST
                                // equal sizeof(SockAddrIn).
        SYS_SENDTO = 57,        // (fd, const void* buf, len, const struct SockAddrIn* dst, addrlen) ->
                                // bytes sent or negative errno. payload bounded by
                                // SYS_IO_MAX_LEN / UDP_MAX_PAYLOAD.
        SYS_RECVFROM = 58,      // (fd, void* buf, len, struct SockAddrIn* src_out, uint32_t* addrlen_io) ->
                                // bytes received or negative errno. Bounded RX
                                // advance plus bounded wait; -EAGAIN on no data.
                                // This is bounded, NOT general POSIX blocking.
        SYS_DYN_MAP = 59,       // (base, len, permissions) -> base or negative errno. Default-off bounded
                                // dynamic-link path only: reserves demand-zero pages at a fixed page-aligned
                                // base inside the bounded shared-object region for the user-space interpreter.
        SYS_DYN_PROTECT = 60,   // (base, len, permissions) -> 0 or negative errno. Default-off bounded
                                // dynamic-link path only: changes permissions of an already-mapped
                                // shared-object sub-range (e.g. dropping write after copying text bytes).
        SYS_POLL = 61,          // (rdi=pollfd*, rsi=nfds, rdx=timeout_ms) -> ready descriptor count,
                                // 0 on timeout with none ready, or negative errno
                                // (-EINVAL when nfds exceeds POLL_MAX_FDS, -EFAULT on a
                                // bad user array). Bounded multiplexing (poll-style):
                                // fixed-capacity descriptor set, millisecond timeout,
                                // level-triggered readiness reusing vfs::poll_file and
                                // the scheduler multi-queue wait. NOT full POSIX
                                // poll/select/epoll/ppoll, no unbounded/dynamic sets, no
                                // edge-triggered/event-object/priority/signal-restart
                                // semantics.
        SYS_CONNECT = 62,       // (fd, const struct SockAddrIn*, addrlen) -> 0 / -EINPROGRESS
                                // (nonblocking in progress) / negative errno. Active-open
                                // a stream (TCP) socket to an IPv4 host:port. addrlen MUST
                                // equal sizeof(SockAddrIn). Blocking fd blocks to
                                // ESTABLISHED; nonblocking returns -EINPROGRESS and the
                                // result is read later via getsockopt(SO_ERROR).
        SYS_LISTEN = 63,        // (fd, backlog) -> 0 or negative errno. Mark a bound stream
                                // socket passive; backlog is clamped to the compile-time
                                // STREAM_ACCEPT_QUEUE_CAPACITY.
        SYS_ACCEPT = 64,        // (fd, struct SockAddrIn* peer_out, uint32_t* addrlen_io) ->
                                // new connection fd or negative errno. Blocking fd blocks
                                // until a completed connection exists; nonblocking returns
                                // -EAGAIN. peer_out may be null (address not written back).
        SYS_GETSOCKOPT = 65,    // (fd, level, optname, void* optval, uint32_t* optlen_io) ->
                                // 0 or negative errno. Bounded: ONLY SOL_SOCKET/SO_ERROR is
                                // supported (reads and clears the connection's pending
                                // error for nonblocking connect completion). Any other
                                // level/optname returns -ENOPROTOOPT. No setsockopt.
        SYS_SEND = 66,          // (fd, const void* buf, len, flags) -> bytes sent or negative
                                // errno. Stream socket send; data path equals write. flags
                                // recognizes only MSG_NOSIGNAL (suppress SIGPIPE on a
                                // broken-pipe write, returning -EPIPE); any other non-zero
                                // flag bit returns -EINVAL. write == send(..., flags=0).
    };

    // Bounded user-visible poll(2)-style descriptor entry. Fixed-size; the mirror
    // user/libc header user/libc/include/poll.h MUST match this layout and the
    // POLL* event bits below. fd < 0 means "ignore this entry" (revents cleared).
    struct pollfd {
        int32_t fd;        // watched descriptor; negative to ignore this entry
        uint16_t events;   // requested event bits (POLLIN/POLLOUT subset)
        uint16_t revents;  // kernel-filled ready bits (may add POLLERR/POLLHUP/POLLNVAL)
    };

    // Bounded poll event bits (values aligned with common Linux poll(2) constants
    // for portability). POLLERR/POLLHUP/POLLNVAL are output-only: the kernel fills
    // them under the corresponding condition even if events did not request them.
    constexpr uint16_t POLLIN = 0x001;    // readable
    constexpr uint16_t POLLOUT = 0x004;   // writable
    constexpr uint16_t POLLERR = 0x008;   // error condition (output only)
    constexpr uint16_t POLLHUP = 0x010;   // peer hung up (output only)
    constexpr uint16_t POLLNVAL = 0x020;  // invalid descriptor (output only)

    // Fixed capacity of a single SYS_POLL descriptor set. nfds > POLL_MAX_FDS is
    // rejected with -EINVAL; nfds == 0 is legal (degenerate timeout-only wait).
    constexpr uint64_t POLL_MAX_FDS = 16;

    // Bounded user-visible UDP socket address (BigOS sockaddr-lite). Fixed-size,
    // host byte order, IPv4 + port only. It deliberately avoids POSIX sockaddr
    // variable length, sa_family_t, and network-byte-order complexity. The mirror
    // user/libc header user/libc/include/sys/socket.h MUST match this layout and
    // the constants below.
    struct SockAddrIn {
        uint16_t family;   // SOCKET_AF_INET
        uint16_t port;     // local/remote UDP port, host order
        uint32_t addr;     // IPv4 address, host order
    };

    // Bounded socket domain/type/protocol subset accepted by SYS_SOCKET.
    constexpr uint16_t SOCKET_AF_INET = 2;
    constexpr uint16_t SOCKET_SOCK_DGRAM = 2;
    constexpr uint16_t SOCKET_SOCK_STREAM = 1;
    constexpr uint16_t SOCKET_IPPROTO_UDP = 17;
    constexpr uint16_t SOCKET_IPPROTO_TCP = 6;

    // Bounded getsockopt level/option: ONLY SOL_SOCKET/SO_ERROR is supported (see
    // SYS_GETSOCKOPT). Values follow the conventional Linux numbers for mirror
    // consistency; anything else returns -ENOPROTOOPT.
    constexpr uint64_t SOL_SOCKET = 1;
    constexpr uint64_t SO_ERROR = 4;

    // Bounded send(2) flag: MSG_NOSIGNAL suppresses SIGPIPE on a broken-pipe write
    // (SYS_SEND). It is the only recognized flag; any other non-zero bit is
    // rejected with -EINVAL.
    constexpr uint64_t MSG_NOSIGNAL = 0x4000;

    // POSIX-style error codes live in bigos/errno.h (single source of truth);
    // the dispatcher writes their negated value into the return register, e.g.
    // -bigos::ENOSYS for an unknown syscall number.
    constexpr uint64_t SYS_WRITE_MAX_LEN = 128;
    constexpr uint64_t SYS_IO_MAX_LEN = 512;
    constexpr uint64_t SYS_PATH_MAX_LEN = 256;
    constexpr uint64_t SYS_DIRENT_MAX_ENTRIES = 16;

    // Syscall dispatch entry. Invoked from irq_dispatch when the interrupt vector
    // is VECTOR_SYSCALL. Reads the syscall number from frame->rax, routes to the
    // matching kernel implementation, and writes the result (or -bigos::ENOSYS
    // for an unknown number) back into frame->rax. It MUST NOT send an i8259 EOI
    // (syscall is not an external IRQ). fd/VFS syscalls additionally check the
    // scheduler blocking guard before allocating or entering synchronous storage IO.
    void dispatch(bigos::irq::InterruptFrame *__frame) noexcept;
}   // namespace bigos::sys

#endif   // _BIG_SYSCALL_H
