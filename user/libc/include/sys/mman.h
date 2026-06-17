/* BigOS bounded anonymous mapping lifecycle declarations.
 *
 * This header intentionally exposes BigOS-specific helpers, not complete POSIX
 * mmap/munmap/mprotect semantics. Ranges must be page-aligned, private
 * anonymous VMAs must fully cover them, errors are reported through errno, and
 * MAP_FIXED/shared writable/file-backed writable behavior is unsupported. */
#ifndef _BIGOS_USER_SYS_MMAN_H
#define _BIGOS_USER_SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_FAILED ((void *)-1)

void *mmap_anon(size_t len, long prot, long flags);
int bigos_munmap_anon(void *addr, size_t len);
int bigos_mprotect_anon(void *addr, size_t len, long prot);

#endif /* _BIGOS_USER_SYS_MMAN_H */
