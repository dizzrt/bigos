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
constexpr int ESRCH = 3;          // no such process
constexpr int EBADF = 9;          // bad file descriptor
constexpr int ECHILD = 10;        // no child processes
constexpr int EWOULDBLOCK = 11;   // operation would block (aliased to EAGAIN)
constexpr int EAGAIN = 11;        // resource temporarily unavailable (e.g. process soft limit reached)
constexpr int ENOMEM = 12;        // out of kernel memory
constexpr int EFAULT = 14;        // bad address
constexpr int EINVAL = 22;        // invalid argument
constexpr int EMFILE = 24;        // too many open files
constexpr int ENOSYS = 38;        // function not implemented
NAMESPACE_BIGOS_END

#endif   // _BIG_ERRNO_H
