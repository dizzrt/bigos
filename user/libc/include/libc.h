/* BigOS bounded user-space libc umbrella header.
 *
 * Freestanding C declarations for the user runtime: syscall wrappers (POSIX
 * errno convention), string/memory routines, bounded brk-based allocation,
 * minimal fd-backed stdio, read-only environ/getenv, and BigOS bounded directory
 * helpers. This re-exports the fine-grained public headers for existing BigOS
 * programs.
 *
 * Raw syscall primitives are intentionally not exported here. Low-level callers
 * that opt in to the BigOS raw ABI must include <bigos_syscall.h> directly.
 *
 * It does NOT implement hosted libc, full FILE streams, locale, threads,
 * dynamic loading, shared libraries, or full POSIX semantics. */
#ifndef _BIGOS_LIBC_H
#define _BIGOS_LIBC_H

#include "errno.h"        /* IWYU pragma: export */
#include "bigos_dirent.h" /* IWYU pragma: export */
#include "fcntl.h"        /* IWYU pragma: export */
#include "signal.h"       /* IWYU pragma: export */
#include "stdio.h"        /* IWYU pragma: export */
#include "stdlib.h"       /* IWYU pragma: export */
#include "string.h"       /* IWYU pragma: export */
#include "sys/mman.h"     /* IWYU pragma: export */
#include "sys/stat.h"     /* IWYU pragma: export */
#include "sys/types.h"    /* IWYU pragma: export */
#include "sys/wait.h"     /* IWYU pragma: export */
#include "time.h"         /* IWYU pragma: export */
#include "unistd.h"       /* IWYU pragma: export */

#endif /* _BIGOS_LIBC_H */
