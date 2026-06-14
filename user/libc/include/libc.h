/* BigOS minimal user-space libc umbrella header.
 *
 * Freestanding C declarations for the user runtime: syscall wrappers (POSIX
 * errno convention), minimal string/memory routines, a bounded brk-based
 * malloc/free, minimal stdio/standard streams, and read-only environ/getenv.
 * This re-exports the bounded fine-grained headers for existing BigOS programs.
 * It does NOT implement hosted libc, full FILE streams, locale, threads,
 * dynamic loading, or full POSIX semantics. */
#ifndef _BIGOS_LIBC_H
#define _BIGOS_LIBC_H

#include "errno.h"        /* IWYU pragma: export */
#include "bigos_dirent.h" /* IWYU pragma: export */
#include "fcntl.h"        /* IWYU pragma: export */
#include "signal.h"       /* IWYU pragma: export */
#include "stdio.h"        /* IWYU pragma: export */
#include "stdlib.h"       /* IWYU pragma: export */
#include "string.h"       /* IWYU pragma: export */
#include "sys/stat.h"     /* IWYU pragma: export */
#include "sys/types.h"    /* IWYU pragma: export */
#include "sys/wait.h"     /* IWYU pragma: export */
#include "time.h"         /* IWYU pragma: export */
#include "unistd.h"       /* IWYU pragma: export */

/* --- BigOS-specific raw syscall primitives ---
 *
 * Low-level ABI helpers for libc and explicitly BigOS-specific callers only:
 * rax=number, args=rdi/rsi/rdx/r10/r8/r9, raw kernel return in rax. Higher-level
 * wrappers remain responsible for errno translation. This is not POSIX
 * syscall(2) compatibility and must not be used to imply unsupported syscalls. */
long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall4(long n, long a0, long a1, long a2, long a3);
long syscall5(long n, long a0, long a1, long a2, long a3, long a4);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

#endif /* _BIGOS_LIBC_H */
