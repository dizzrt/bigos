/* BigOS user libc: minimal stdio.
 *
 * Thin wrappers over fd 0/1/2 read/write plus putchar/puts and a minimal printf
 * supporting %s, %d, %x, %c, and %%. No FILE buffering semantics. */
#include "libc.h"
#include <stdarg.h>

int putchar(int c) {
    char ch = (char)c;
    if (write(1, &ch, 1) != 1)
        return -1;
    return c;
}

int puts(const char *s) {
    size_t len = strlen(s);
    if (len != 0 && write(1, s, len) != (ssize_t)len)
        return -1;
    if (putchar('\n') < 0)
        return -1;
    return (int)len + 1;
}

static int print_str(const char *s, int *count) {
    if (s == NULL)
        s = "(null)";
    size_t len = strlen(s);
    if (len != 0) {
        if (write(1, s, len) != (ssize_t)len)
            return -1;
        *count += (int)len;
    }
    return 0;
}

static int print_uint(unsigned long value, unsigned base, int *count) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    int i = 0;
    if (value == 0)
        buf[i++] = '0';
    while (value != 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }
    while (i > 0) {
        if (putchar(buf[--i]) < 0)
            return -1;
        (*count)++;
    }
    return 0;
}

static int print_int(long value, int *count) {
    if (value < 0) {
        if (putchar('-') < 0)
            return -1;
        (*count)++;
        return print_uint((unsigned long)(-value), 10, count);
    }
    return print_uint((unsigned long)value, 10, count);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int count = 0;
    for (const char *p = fmt; *p != 0; p++) {
        if (*p != '%') {
            if (putchar(*p) < 0) {
                va_end(ap);
                return -1;
            }
            count++;
            continue;
        }
        p++;
        switch (*p) {
            case 's':
                if (print_str(va_arg(ap, const char *), &count) < 0) {
                    va_end(ap);
                    return -1;
                }
                break;
            case 'd':
                if (print_int((long)va_arg(ap, int), &count) < 0) {
                    va_end(ap);
                    return -1;
                }
                break;
            case 'x':
                if (print_uint((unsigned long)va_arg(ap, unsigned int), 16, &count) < 0) {
                    va_end(ap);
                    return -1;
                }
                break;
            case 'c':
                if (putchar((char)va_arg(ap, int)) < 0) {
                    va_end(ap);
                    return -1;
                }
                count++;
                break;
            case '%':
                if (putchar('%') < 0) {
                    va_end(ap);
                    return -1;
                }
                count++;
                break;
            case 0:
                va_end(ap);
                return count;
            default:
                if (putchar('%') < 0 || putchar(*p) < 0) {
                    va_end(ap);
                    return -1;
                }
                count += 2;
                break;
        }
    }
    va_end(ap);
    return count;
}
