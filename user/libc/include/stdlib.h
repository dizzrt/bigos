/* BigOS minimal stdlib declarations.
 *
 * Provides the bounded brk-backed allocator, process exit, and read-only
 * environment lookup. No hosted allocator, locale, conversion, random, or
 * environment mutation APIs are provided. */
#ifndef _BIGOS_USER_STDLIB_H
#define _BIGOS_USER_STDLIB_H

#include <sys/types.h>

void exit(int code) __attribute__((noreturn));
void *malloc(size_t n);
void free(void *p);

extern char **environ;
char *getenv(const char *name);

#endif /* _BIGOS_USER_STDLIB_H */
