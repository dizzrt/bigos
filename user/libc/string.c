/* BigOS user libc: minimal freestanding string and memory routines. */
#include "libc.h"

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n] != 0)
        n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb)
            return (int)ca - (int)cb;
        if (ca == 0)
            return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *out = dst;
    while ((*dst++ = *src++) != 0) {
    }
    return out;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != 0; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == 0)
            return NULL;
    }
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (*s == 0)
            break;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == 0)
        return (char *)haystack;
    for (const char *h = haystack; *h != 0; h++) {
        const char *a = h;
        const char *b = needle;
        while (*a != 0 && *b != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*b == 0)
            return (char *)h;
    }
    return NULL;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char target = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == target)
            return (void *)(p + i);
    }
    return NULL;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (unsigned char)c;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

const char *strerror(int errnum) {
    switch (errnum) {
        case 0:
            return "Success";
        case EPERM:
            return "Operation not permitted";
        case ENOENT:
            return "No such file or directory";
        case ESRCH:
            return "No such process";
        case EIO:
            return "Input/output error";
        case E2BIG:
            return "Argument list too long";
        case ENOEXEC:
            return "Exec format error";
        case EBADF:
            return "Bad file descriptor";
        case ECHILD:
            return "No child processes";
        case EAGAIN:
            return "Resource temporarily unavailable";
        case ENOMEM:
            return "Out of memory";
        case EACCES:
            return "Permission denied";
        case EFAULT:
            return "Bad address";
        case EEXIST:
            return "File exists";
        case ENODEV:
            return "No such device";
        case ENOTDIR:
            return "Not a directory";
        case EISDIR:
            return "Is a directory";
        case EINVAL:
            return "Invalid argument";
        case EMFILE:
            return "Too many open files";
        case ENOSPC:
            return "No space left on device";
        case ESPIPE:
            return "Illegal seek";
        case EROFS:
            return "Read-only file system";
        case EPIPE:
            return "Broken pipe";
        case ERANGE:
            return "Result too large";
        case ENOSYS:
            return "Function not implemented";
        case ENOTEMPTY:
            return "Directory not empty";
        case EOPNOTSUPP:
            return "Operation not supported";
        default:
            return "Unknown error";
    }
}
