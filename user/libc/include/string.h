/* BigOS minimal string/memory declarations.
 *
 * Bounded freestanding subset only. Null pointer inputs follow the ordinary C
 * preconditions for these routines; this libc does not add hosted safety checks. */
#ifndef _BIGOS_USER_STRING_H
#define _BIGOS_USER_STRING_H

#include <sys/types.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
const char *strerror(int errnum);

#endif /* _BIGOS_USER_STRING_H */
