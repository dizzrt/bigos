/* BigOS minimal stdlib declarations.
 *
 * Provides bounded brk-backed allocation, integer conversion, sort/search with
 * explicit caller comparators, process exit, and read-only environment lookup.
 * No hosted allocator, locale, random, floating-point conversion, or
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
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
int atoi(const char *nptr);
int abs(int v);
long labs(long v);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

extern char **environ;
char *getenv(const char *name);

#endif /* _BIGOS_USER_STDLIB_H */
