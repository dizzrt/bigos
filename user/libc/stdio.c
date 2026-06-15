/* BigOS user libc: bounded fd-backed stdio.
 *
 * Thin wrappers over fd 0/1/2 read/write plus putchar/puts, printf/fprintf, and
 * bounded snprintf. Supports strings, signed/unsigned integers, simple width,
 * pointers, and size/long length modifiers. No fopen, fclose, buffering,
 * precision, locale, floating point, or hosted FILE semantics. */
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

struct format_sink {
    int fd;
    char *buf;
    size_t size;
    int total;
    int failed;
};

static void sink_char(struct format_sink *sink, char c) {
    if (sink->failed)
        return;
    if (sink->buf != NULL) {
        if (sink->size != 0 && (size_t)sink->total + 1 < sink->size)
            sink->buf[sink->total] = c;
    } else if (write_char_fd(sink->fd, c) < 0) {
        sink->failed = 1;
        return;
    }
    sink->total++;
}

static void sink_repeat(struct format_sink *sink, char c, int count) {
    while (count-- > 0)
        sink_char(sink, c);
}

static void sink_bytes(struct format_sink *sink, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++)
        sink_char(sink, s[i]);
}

static void format_string(struct format_sink *sink, const char *s, int width) {
    if (s == NULL)
        s = "(null)";
    size_t len = strlen(s);
    if (width > (int)len)
        sink_repeat(sink, ' ', width - (int)len);
    sink_bytes(sink, s, len);
}

static void reverse_buffer(char *buf, int len) {
    for (int i = 0; i < len / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = tmp;
    }
}

static void format_unsigned(struct format_sink *sink, unsigned long value, unsigned base, int width, const char *prefix) {
    char tmp[32];
    const char *digits = "0123456789abcdef";
    int len = 0;
    if (value == 0)
        tmp[len++] = '0';
    while (value != 0) {
        tmp[len++] = digits[value % base];
        value /= base;
    }
    reverse_buffer(tmp, len);
    int prefix_len = prefix != NULL ? (int)strlen(prefix) : 0;
    int out_len = prefix_len + len;
    if (width > out_len)
        sink_repeat(sink, ' ', width - out_len);
    if (prefix_len != 0)
        sink_bytes(sink, prefix, (size_t)prefix_len);
    sink_bytes(sink, tmp, (size_t)len);
}

static void format_signed(struct format_sink *sink, long value, int width) {
    unsigned long magnitude;
    const char *prefix = NULL;
    if (value < 0) {
        prefix = "-";
        magnitude = (unsigned long)(-(value + 1)) + 1ul;
    } else {
        magnitude = (unsigned long)value;
    }
    format_unsigned(sink, magnitude, 10, width, prefix);
}

static int format_core(struct format_sink *sink, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p != 0; p++) {
        if (*p != '%') {
            sink_char(sink, *p);
            continue;
        }
        p++;
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        int long_arg = 0;
        int size_arg = 0;
        if (*p == 'l') {
            long_arg = 1;
            p++;
        } else if (*p == 'z') {
            size_arg = 1;
            p++;
        }
        switch (*p) {
            case 's':
                format_string(sink, va_arg(ap, const char *), width);
                break;
            case 'd':
            case 'i':
                if (long_arg)
                    format_signed(sink, va_arg(ap, long), width);
                else if (size_arg)
                    format_signed(sink, (long)va_arg(ap, size_t), width);
                else
                    format_signed(sink, (long)va_arg(ap, int), width);
                break;
            case 'u':
                if (long_arg)
                    format_unsigned(sink, va_arg(ap, unsigned long), 10, width, NULL);
                else if (size_arg)
                    format_unsigned(sink, (unsigned long)va_arg(ap, size_t), 10, width, NULL);
                else
                    format_unsigned(sink, (unsigned long)va_arg(ap, unsigned int), 10, width, NULL);
                break;
            case 'x':
                if (long_arg)
                    format_unsigned(sink, va_arg(ap, unsigned long), 16, width, NULL);
                else if (size_arg)
                    format_unsigned(sink, (unsigned long)va_arg(ap, size_t), 16, width, NULL);
                else
                    format_unsigned(sink, (unsigned long)va_arg(ap, unsigned int), 16, width, NULL);
                break;
            case 'p':
                format_unsigned(sink, (unsigned long)va_arg(ap, void *), 16, width, "0x");
                break;
            case 'c':
                if (width > 1)
                    sink_repeat(sink, ' ', width - 1);
                sink_char(sink, (char)va_arg(ap, int));
                break;
            case '%':
                sink_char(sink, '%');
                break;
            case 0:
                return sink->failed ? -1 : sink->total;
            default:
                sink_char(sink, '%');
                if (long_arg)
                    sink_char(sink, 'l');
                else if (size_arg)
                    sink_char(sink, 'z');
                sink_char(sink, *p);
                break;
        }
    }
    return sink->failed ? -1 : sink->total;
}

static int vfprintf_fd(int fd, const char *fmt, va_list ap) {
    struct format_sink sink = { .fd = fd, .buf = NULL, .size = 0, .total = 0, .failed = 0 };
    return format_core(&sink, fmt, ap);
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

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    if (buf == NULL && size != 0) {
        errno = EINVAL;
        return -1;
    }
    struct format_sink sink = { .fd = -1, .buf = buf, .size = size, .total = 0, .failed = 0 };
    va_list ap;
    va_start(ap, fmt);
    int count = format_core(&sink, fmt, ap);
    va_end(ap);
    if (buf != NULL && size != 0) {
        size_t index = (size_t)sink.total < size ? (size_t)sink.total : size - 1;
        buf[index] = 0;
    }
    return count;
}

void perror(const char *s) {
    int fd = stream_fd(stderr);
    if (fd < 0)
        return;
    if (s != NULL && s[0] != 0) {
        struct format_sink sink = { .fd = fd, .buf = NULL, .size = 0, .total = 0, .failed = 0 };
        format_string(&sink, s, 0);
        format_string(&sink, ": ", 0);
        if (sink.failed)
            return;
    }
    struct format_sink sink = { .fd = fd, .buf = NULL, .size = 0, .total = 0, .failed = 0 };
    format_string(&sink, strerror(errno), 0);
    if (sink.failed)
        return;
    (void)write_char_fd(fd, '\n');
}
