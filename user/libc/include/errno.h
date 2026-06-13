/* BigOS user-space errno mirror header.
 *
 * Mirror of include/bigos/errno.h positive POSIX-style error codes. The kernel
 * returns the negated value in rax; the libc syscall wrappers translate a
 * negative return into errno = positive value and a -1/sentinel result. The
 * values MUST match the kernel header; a source-contract test under tests/
 * asserts equality. Plain C, freestanding. */
#ifndef _BIGOS_USER_ERRNO_H
#define _BIGOS_USER_ERRNO_H

#define EPERM       1
#define ENOENT      2
#define ESRCH       3
#define EIO         5
#define E2BIG       7
#define ENOEXEC     8
#define EBADF       9
#define ECHILD      10
#define EWOULDBLOCK 11
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EEXIST      17
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define EMFILE      24
#define ENOSPC      28
#define ESPIPE      29
#define EROFS       30
#define EPIPE       32
#define ERANGE      34
#define ENOSYS      38

/* Global errno set by the syscall wrappers on a negative kernel return. */
extern int errno;

#endif /* _BIGOS_USER_ERRNO_H */
