/* BigOS minimal user-space type definitions.
 *
 * Freestanding subset only: these are the BigOS-owned types used by the
 * user-space syscall wrappers and simple static C programs. `size_t` and `NULL`
 * are intentionally NOT defined here; they come from the cross-toolchain
 * freestanding <stddef.h> to avoid duplicate typedef/macro conflicts when a
 * program includes both this header and a standard freestanding header. */
#ifndef _BIGOS_USER_SYS_TYPES_H
#define _BIGOS_USER_SYS_TYPES_H

#include <stddef.h> /* size_t, NULL from the toolchain freestanding header */

typedef long ssize_t;
typedef long off_t;
typedef unsigned int mode_t;
typedef int pid_t;

#endif /* _BIGOS_USER_SYS_TYPES_H */
