/* BigOS minimal user-space type definitions.
 *
 * Freestanding subset only: these are the types currently used by BigOS
 * user-space syscall wrappers and simple static C programs. */
#ifndef _BIGOS_USER_SYS_TYPES_H
#define _BIGOS_USER_SYS_TYPES_H

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef int pid_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* _BIGOS_USER_SYS_TYPES_H */
