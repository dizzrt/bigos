/* BigOS-specific raw syscall primitives.
 *
 * Opt-in low-level ABI helpers for libc internals and explicitly BigOS-specific
 * callers. They use rax=number, args=rdi/rsi/rdx/r10/r8/r9, and return the raw
 * kernel rax value. They do not translate errno and are not POSIX syscall(2).
 */
#ifndef _BIGOS_USER_BIGOS_SYSCALL_H
#define _BIGOS_USER_BIGOS_SYSCALL_H

long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall4(long n, long a0, long a1, long a2, long a3);
long syscall5(long n, long a0, long a1, long a2, long a3, long a4);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

#endif /* _BIGOS_USER_BIGOS_SYSCALL_H */
