#ifndef _BIG_ERRNO_H
#define _BIG_ERRNO_H

#include <bigos/types.h>

// Single source of truth for the kernel's POSIX-style error codes.
//
// Values are the conventional positive POSIX/Linux x86_64 errno numbers. The
// kernel writes the corresponding negative value into the syscall return
// register (rax) by convention, e.g. `-bigos::EBADF`. This header is
// freestanding-safe: it exposes only compile-time integer constants and does
// not depend on libc, a user-space `errno` global, or any OS service.
//
// Subsystem headers MUST NOT define their own duplicate error-code constants;
// they MUST reference these definitions instead. Non-POSIX error expressions
// (the scheduler's WAIT_TIMEOUT/WAIT_BLOCK_FORBIDDEN and the block driver's
// BlockStatus enum) are intentionally out of scope and keep their own values.

NAMESPACE_BIGOS_BEG
constexpr int EPERM = 1;          // operation not permitted
constexpr int ENOENT = 2;         // no such file or directory
constexpr int ESRCH = 3;          // no such process
constexpr int EIO = 5;            // input/output error
constexpr int E2BIG = 7;          // argument list too long
constexpr int ENOEXEC = 8;        // exec format error
constexpr int EBADF = 9;          // bad file descriptor
constexpr int ECHILD = 10;        // no child processes
constexpr int EWOULDBLOCK = 11;   // operation would block (aliased to EAGAIN)
constexpr int EAGAIN = 11;        // resource temporarily unavailable (e.g. process soft limit reached)
constexpr int ENOMEM = 12;        // out of kernel memory
constexpr int EACCES = 13;        // permission denied
constexpr int EFAULT = 14;        // bad address
constexpr int EEXIST = 17;        // file exists
constexpr int ENODEV = 19;        // no such device / runtime backend not initialized
constexpr int ENOTDIR = 20;       // not a directory
constexpr int EISDIR = 21;        // is a directory
constexpr int EINVAL = 22;        // invalid argument
constexpr int EMFILE = 24;        // too many open files
constexpr int ENOSPC = 28;        // no space left on device
constexpr int ESPIPE = 29;        // illegal seek (not seekable, e.g. pipe)
constexpr int EROFS = 30;         // read-only file system
constexpr int EPIPE = 32;         // broken pipe (all read ends closed)
constexpr int ERANGE = 34;        // result too large for caller buffer
constexpr int ENOSYS = 38;        // function not implemented
constexpr int ENOTEMPTY = 39;    // directory not empty
constexpr int ENOTSOCK = 88;      // socket operation on non-socket fd
constexpr int EDESTADDRREQ = 89;  // destination address required (e.g. unbound/no-peer UDP)
constexpr int EMSGSIZE = 90;      // message too long for the bounded datagram limit
constexpr int ENOPROTOOPT = 92;   // protocol option not available (getsockopt only supports SO_ERROR)
constexpr int EADDRINUSE = 98;    // address/port already in use
constexpr int ENETUNREACH = 101;  // network unreachable / no route
constexpr int ECONNRESET = 104;   // connection reset by peer (RST / retransmit limit)
constexpr int ENOBUFS = 105;      // no buffer space available
constexpr int EISCONN = 106;      // socket is already connected
constexpr int ENOTCONN = 107;     // socket is not connected
constexpr int ETIMEDOUT = 110;    // operation timed out
constexpr int ECONNREFUSED = 111; // connection refused (no listener / actively refused)
constexpr int EHOSTUNREACH = 113; // host unreachable (e.g. ARP unresolved / timeout)
constexpr int EALREADY = 114;     // connection attempt already in progress
constexpr int EINPROGRESS = 115;  // nonblocking connect handshake in progress
constexpr int EOPNOTSUPP = 95;    // operation not supported on this object/backend
NAMESPACE_BIGOS_END

#endif   // _BIG_ERRNO_H
