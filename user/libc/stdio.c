/* BigOS user libc: minimal stdio.
 *
 * Thin wrappers over fd 0/1/2 read/write plus putchar/puts, minimal printf, and
 * fprintf for the standard streams. Supports %s, %d, %x, %c, and %%. No fopen,
 * fclose, buffering, locale, or hosted FILE semantics. */
#include "libc.h"
#include <stdarg.h>

static int g_stdin_file;
static int g_stdout_file;
static int g_stderr_file;

FILE *stdin = (FILE *)&g_stdin_file;
FILE *stdout = (FILE *)&g_stdout_file;
FILE *stderr = (FILE *)&g_stderr_file;

static int stream_fd(FILE *stream) {
    if (stream == stdin)
        return 0;
    if (stream == stdout)
        return 1;
    if (stream == stderr)
        return 2;
    return -1;
}

static int write_char_fd(int fd, int c) {
    char ch = (char)c;
    if (write(fd, &ch, 1) != 1)
        return -1;
    return c;
}

int putchar(int c) {
    int fd = stream_fd(stdout);
    if (fd < 0)
        return -1;
    return write_char_fd(fd, c);
}

int puts(const char *s) {
    int fd = stream_fd(stdout);
    if (fd < 0)
        return -1;
    size_t len = strlen(s);
    if (len != 0 && write(fd, s, len) != (ssize_t)len)
        return -1;
    if (write_char_fd(fd, '\n') < 0)
        return -1;
    return (int)len + 1;
}

static int print_str(int fd, const char *s, int *count) {
    if (s == NULL)
        s = "(null)";
    size_t len = strlen(s);
    if (len != 0) {
        if (write(fd, s, len) != (ssize_t)len)
            return -1;
        *count += (int)len;
    }
    return 0;
}

static int print_uint(int fd, unsigned long value, unsigned base, int *count) {
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
        if (write_char_fd(fd, buf[--i]) < 0)
            return -1;
        (*count)++;
    }
    return 0;
}

static int print_int(int fd, long value, int *count) {
    if (value < 0) {
        if (write_char_fd(fd, '-') < 0)
            return -1;
        (*count)++;
        return print_uint(fd, (unsigned long)(-value), 10, count);
    }
    return print_uint(fd, (unsigned long)value, 10, count);
}

static int vfprintf_fd(int fd, const char *fmt, va_list ap) {
    int count = 0;
    for (const char *p = fmt; *p != 0; p++) {
        if (*p != '%') {
            if (write_char_fd(fd, *p) < 0)
                return -1;
            count++;
            continue;
        }
        p++;
        switch (*p) {
            case 's':
                if (print_str(fd, va_arg(ap, const char *), &count) < 0)
                    return -1;
                break;
            case 'd':
                if (print_int(fd, (long)va_arg(ap, int), &count) < 0)
                    return -1;
                break;
            case 'x':
                if (print_uint(fd, (unsigned long)va_arg(ap, unsigned int), 16, &count) < 0)
                    return -1;
                break;
            case 'c':
                if (write_char_fd(fd, (char)va_arg(ap, int)) < 0)
                    return -1;
                count++;
                break;
            case '%':
                if (write_char_fd(fd, '%') < 0)
                    return -1;
                count++;
                break;
            case 0:
                return count;
            default:
                if (write_char_fd(fd, '%') < 0 || write_char_fd(fd, *p) < 0)
                    return -1;
                count += 2;
                break;
        }
    }
    return count;
}

int printf(const char *fmt, ...) {
    int fd = stream_fd(stdout);
    if (fd < 0)
        return -1;
    va_list ap;
    va_start(ap, fmt);
    int count = vfprintf_fd(fd, fmt, ap);
    va_end(ap);
    return count;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    int fd = stream_fd(stream);
    if (fd < 0)
        return -1;
    va_list ap;
    va_start(ap, fmt);
    int count = vfprintf_fd(fd, fmt, ap);
    va_end(ap);
    return count;
}
