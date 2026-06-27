/* BigOS minimal string/memory declarations.
 *
 * Bounded freestanding subset only. Null pointer inputs follow the ordinary C
 * preconditions for these routines; this libc does not add hosted safety checks.
 * The tokenizer is the explicit reentrant strtok_r (caller-provided saveptr);
 * the hidden-global strtok is intentionally not provided. */
#ifndef _BIGOS_USER_STRING_H
#define _BIGOS_USER_STRING_H

#include <sys/types.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strtok_r(char *str, const char *delim, char **saveptr);
void *memcpy(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
const char *strerror(int errnum);

#endif /* _BIGOS_USER_STRING_H */
