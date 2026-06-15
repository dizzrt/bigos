/* BigOS minimal stdlib declarations.
 *
 * Provides bounded brk-backed allocation, integer conversion, process exit, and
 * read-only environment lookup. No hosted allocator, locale, random, or
 * environment mutation APIs are provided. */
#ifndef _BIGOS_USER_STDLIB_H
#define _BIGOS_USER_STDLIB_H

#include <sys/types.h>

void exit(int code) __attribute__((noreturn));
void *malloc(size_t n);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *p);
long strtol(const char *nptr, char **endptr, int base);
int atoi(const char *nptr);

extern char **environ;
char *getenv(const char *name);

#endif /* _BIGOS_USER_STDLIB_H */
