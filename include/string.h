#ifndef _BIG_STRING_H
#define _BIG_STRING_H

#include <bigos/types.h>
#include <bigos/attributes.h>

void *memset(void *_s, int _c, size_t n) noexcept;
void *memcpy(void *_dest, const void *_src, size_t _n) noexcept;
// void *memmove(void *_dest, const void *_src, size_t _n) noexcept;
// void *memchr(const void *_s, int _c, size_t _n) noexcept _attr_pure_;
// int memcmp(const void *_s1, const void *_s2) noexcept _attr_pure_;

// char *strcat(char *_dest, const char *src) noexcept;
// char *strncat(char *_dest, const char *_src) noexcept;
// strchr
// char *strrchr(const char *_s, int _c) noexcept _attr_pure_;
// int strcmp(const char *_s1, const char *_s2) noexcept _attr_pure_;
// int strncmp(const char *_s1, const char *_s2) noexcept _attr_pure_;
// int strcoll(const char *_s1, const char *_s2) noexcept _attr_pure_;
// char *strcpy(char *_dest, const char *_src) noexcept;
// char *strerror(int _errnum) noexcept;
// char *strncpy(char *_dest, const char *_src, size_t _n) noexcept;
// char *strstr(const char *_s, const char *_wanted) noexcept _attr_pure_;
// char *strpbrk(const char *_s, const char *_accept) noexcept _attr_pure_;
// char *strtok(char *_s, const char *_delim) noexcept;
// size_t strxfrm(char *_dest, const char *_src, size_t n) noexcept;
// size_t strspn(const char *_s, const char *_accept) noexcept _attr_pure_;
// size_t strcspn(const char *_s, const char *_reject) noexcept _attr_pure_;
size_t strlen(const char *_s) noexcept _attr_pure_;

#endif   // _BIG_STRING_H