/* BigOS user libc: bounded buffered FILE streams.
 *
 * FILE is a libc-internal buffered stream layered over the existing fd/VFS
 * read/write/lseek/close syscalls; all buffering lives in user space. The kernel
 * caps each read/write syscall at SYS_IO_MAX_LEN (512) bytes, so the raw helpers
 * below chunk transfers; the kernel has no O_APPEND, so append mode is emulated
 * by seeking to end before each flush.
 *
 * Supports fopen/freopen/fclose, buffered fread/fwrite, fgetc/getc/fgets/fputc/
 * putc/fputs/ungetc (single-byte pushback), setvbuf/setbuf (three modes),
 * fflush/feof/ferror/clearerr/fileno, and bounded fseek/ftell/rewind. putchar/
 * puts/printf/fprintf route through the stream buffer and the shared formatter;
 * snprintf shares the same formatter against a caller buffer. No scanf family,
 * wide streams, locale, floating point, newline translation, or fpos_t. */
#include "libc.h"
#include <stdarg.h>

/* Kernel I/O caps: file/pipe read+write tolerate SYS_IO_MAX_LEN (512) per
 * syscall, but the fd 1/2 console fast path faults (kills the process) above
 * SYS_WRITE_MAX_LEN (128). Chunk writes at 128 so console and file paths are
 * both safe; reads may use the larger 512 cap. */
#define RAW_WRITE_CHUNK 128
#define RAW_READ_CHUNK 512

enum {
    DIR_NONE = 0,
    DIR_READ = 1,
    DIR_WRITE = 2,
};

enum {
    FLAG_OPEN = 1u << 0,
    FLAG_READABLE = 1u << 1,
    FLAG_WRITABLE = 1u << 2,
    FLAG_OWN_BUF = 1u << 3,  /* buffer was malloc'd by libc */
    FLAG_OWN_FILE = 1u << 4, /* FILE object was malloc'd by libc */
};

struct __bigos_FILE {
    int fd;
    unsigned char *buf;
    size_t bufsize;
    size_t pos; /* read: next byte to consume; write: pending byte count */
    size_t len; /* read: valid bytes in buffer; write: unused */
    int mode;   /* _IOFBF / _IOLBF / _IONBF */
    int dir;    /* DIR_NONE / DIR_READ / DIR_WRITE */
    int eof;
    int error;
    int ungot;      /* pushed-back byte, or -1 */
    int append;     /* append mode: writes go to end */
    int io_started; /* any read/write performed (gates setvbuf) */
    unsigned flags;
    struct __bigos_FILE *next_open;
};

typedef struct __bigos_FILE FILE_impl;

static unsigned char g_stdin_buf[BUFSIZ];
static unsigned char g_stdout_buf[BUFSIZ];

static FILE_impl g_stdin = {
    .fd = 0,
    .buf = g_stdin_buf,
    .bufsize = BUFSIZ,
    .pos = 0,
    .len = 0,
    .mode = _IOFBF,
    .dir = DIR_NONE,
    .eof = 0,
    .error = 0,
    .ungot = -1,
    .append = 0,
    .io_started = 0,
    .flags = FLAG_OPEN | FLAG_READABLE,
    .next_open = NULL,
};

static FILE_impl g_stdout = {
    .fd = 1,
    .buf = g_stdout_buf,
    .bufsize = BUFSIZ,
    .pos = 0,
    .len = 0,
    .mode = _IOLBF,
    .dir = DIR_NONE,
    .eof = 0,
    .error = 0,
    .ungot = -1,
    .append = 0,
    .io_started = 0,
    .flags = FLAG_OPEN | FLAG_WRITABLE,
    .next_open = NULL,
};

static FILE_impl g_stderr = {
    .fd = 2,
    .buf = NULL,
    .bufsize = 0,
    .pos = 0,
    .len = 0,
    .mode = _IONBF,
    .dir = DIR_NONE,
    .eof = 0,
    .error = 0,
    .ungot = -1,
    .append = 0,
    .io_started = 0,
    .flags = FLAG_OPEN | FLAG_WRITABLE,
    .next_open = NULL,
};

FILE *stdin = (FILE *)&g_stdin;
FILE *stdout = (FILE *)&g_stdout;
FILE *stderr = (FILE *)&g_stderr;

/* Registry of fopen'd streams; the three standard streams are flushed
 * explicitly and are intentionally not in this list. */
static FILE_impl *g_open_streams = NULL;

static void register_stream(FILE_impl *s) {
    s->next_open = g_open_streams;
    g_open_streams = s;
}

static void unregister_stream(FILE_impl *s) {
    FILE_impl **link = &g_open_streams;
    while (*link != NULL) {
        if (*link == s) {
            *link = s->next_open;
            s->next_open = NULL;
            return;
        }
        link = &(*link)->next_open;
    }
}

/* Write all of [p, p+n) in <=RAW_WRITE_CHUNK syscalls. Returns bytes written. */
static size_t raw_write_all(int fd, const unsigned char *p, size_t n) {
    size_t done = 0;
    while (done < n) {
        size_t chunk = n - done;
        if (chunk > RAW_WRITE_CHUNK)
            chunk = RAW_WRITE_CHUNK;
        ssize_t w = write(fd, p + done, chunk);
        if (w <= 0)
            break;
        done += (size_t)w;
    }
    return done;
}

/* Flush the write buffer. Returns 0 on success, EOF on error. */
static int flush_write(FILE_impl *s) {
    if (s->dir != DIR_WRITE || s->pos == 0)
        return 0;
    if (s->append && lseek(s->fd, 0, SEEK_END) < 0) {
        s->error = 1;
        s->pos = 0;
        return EOF;
    }
    size_t done = raw_write_all(s->fd, s->buf, s->pos);
    if (done != s->pos) {
        s->error = 1;
        s->pos = 0;
        return EOF;
    }
    s->pos = 0;
    return 0;
}

/* Realign the kernel offset to the logical read position and drop the read
 * buffer, used when switching from reading to writing on a r+/w+/a+ stream. */
static int realign_after_read(FILE_impl *s) {
    size_t unconsumed = s->len - s->pos;
    if (s->ungot != -1)
        unconsumed += 1;
    if (unconsumed > 0 && lseek(s->fd, -(off_t)unconsumed, SEEK_CUR) < 0) {
        s->error = 1;
        return 0;
    }
    s->pos = 0;
    s->len = 0;
    s->ungot = -1;
    return 1;
}

static int to_write(FILE_impl *s) {
    if (!(s->flags & FLAG_OPEN) || !(s->flags & FLAG_WRITABLE)) {
        errno = EBADF;
        s->error = 1;
        return 0;
    }
    if (s->dir == DIR_READ && !realign_after_read(s))
        return 0;
    s->dir = DIR_WRITE;
    return 1;
}

static int to_read(FILE_impl *s) {
    if (!(s->flags & FLAG_OPEN) || !(s->flags & FLAG_READABLE)) {
        errno = EBADF;
        s->error = 1;
        return 0;
    }
    if (s->dir == DIR_WRITE && flush_write(s) != 0)
        return 0;
    s->dir = DIR_READ;
    return 1;
}

/* Refill the read buffer. Returns >0 bytes, 0 on EOF, -1 on error. */
static int fill_read(FILE_impl *s) {
    if (s->buf == NULL || s->bufsize == 0)
        return 0;
    size_t want = s->bufsize;
    if (want > RAW_READ_CHUNK)
        want = RAW_READ_CHUNK;
    ssize_t n = read(s->fd, s->buf, want);
    if (n < 0) {
        s->error = 1;
        return -1;
    }
    if (n == 0) {
        s->eof = 1;
        return 0;
    }
    s->len = (size_t)n;
    s->pos = 0;
    return (int)n;
}

static int parse_mode(const char *m, int *flags, int *readable, int *writable, int *append) {
    if (m == NULL || m[0] == 0)
        return 0;
    int plus = 0;
    char base = m[0];
    for (const char *p = m + 1; *p != 0; p++) {
        if (*p == '+')
            plus = 1;
        else if (*p == 'b')
            ; /* binary == text in BigOS: no newline translation */
        else
            return 0;
    }
    if (base == 'r') {
        *readable = 1;
        *writable = plus;
        *append = 0;
        *flags = plus ? O_RDWR : O_RDONLY;
    } else if (base == 'w') {
        *readable = plus;
        *writable = 1;
        *append = 0;
        *flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    } else if (base == 'a') {
        *readable = plus;
        *writable = 1;
        *append = 1;
        *flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT;
    } else {
        return 0;
    }
    return 1;
}

FILE *fopen(const char *path, const char *mode) {
    if (path == NULL || mode == NULL) {
        errno = EINVAL;
        return NULL;
    }
    int flags = 0;
    int readable = 0;
    int writable = 0;
    int append = 0;
    if (!parse_mode(mode, &flags, &readable, &writable, &append)) {
        errno = EINVAL;
        return NULL;
    }
    int fd = open(path, flags, 0644);
    if (fd < 0)
        return NULL; /* errno set by open */
    if (append)
        lseek(fd, 0, SEEK_END);

    FILE_impl *s = (FILE_impl *)malloc(sizeof(FILE_impl));
    if (s == NULL) {
        int saved = errno;
        close(fd);
        errno = saved != 0 ? saved : ENOMEM;
        return NULL;
    }
    unsigned char *buf = (unsigned char *)malloc(BUFSIZ);
    if (buf == NULL) {
        int saved = errno;
        free(s);
        close(fd);
        errno = saved != 0 ? saved : ENOMEM;
        return NULL;
    }

    s->fd = fd;
    s->buf = buf;
    s->bufsize = BUFSIZ;
    s->pos = 0;
    s->len = 0;
    s->mode = _IOFBF;
    s->dir = DIR_NONE;
    s->eof = 0;
    s->error = 0;
    s->ungot = -1;
    s->append = append;
    s->io_started = 0;
    s->flags = FLAG_OPEN | FLAG_OWN_FILE | FLAG_OWN_BUF | (readable ? FLAG_READABLE : 0) |
               (writable ? FLAG_WRITABLE : 0);
    s->next_open = NULL;
    register_stream(s);
    return (FILE *)s;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || path == NULL || mode == NULL) {
        errno = EINVAL;
        return NULL;
    }
    int flags = 0;
    int readable = 0;
    int writable = 0;
    int append = 0;
    if (!parse_mode(mode, &flags, &readable, &writable, &append)) {
        errno = EINVAL;
        return NULL;
    }

    if (s->flags & FLAG_OPEN) {
        if (s->dir == DIR_WRITE)
            (void)flush_write(s);
        (void)close(s->fd); /* ignore close error to preserve redirect semantics */
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        s->flags &= ~FLAG_OPEN;
        s->fd = -1;
        s->dir = DIR_NONE;
        s->pos = 0;
        s->len = 0;
        s->ungot = -1;
        s->error = 1;
        return NULL; /* errno set by open */
    }
    if (append)
        lseek(fd, 0, SEEK_END);

    s->fd = fd;
    s->dir = DIR_NONE;
    s->pos = 0;
    s->len = 0;
    s->ungot = -1;
    s->eof = 0;
    s->error = 0;
    s->io_started = 0;
    s->mode = (s->buf != NULL) ? _IOFBF : _IONBF;
    s->append = append;
    s->flags = (s->flags & (FLAG_OWN_FILE | FLAG_OWN_BUF)) | FLAG_OPEN |
               (readable ? FLAG_READABLE : 0) | (writable ? FLAG_WRITABLE : 0);
    return (FILE *)s;
}

int fclose(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL) {
        errno = EINVAL;
        return EOF;
    }
    if (!(s->flags & FLAG_OPEN)) {
        errno = EBADF;
        return EOF;
    }
    int ret = 0;
    if (s->dir == DIR_WRITE && flush_write(s) != 0)
        ret = EOF;
    if (close(s->fd) != 0)
        ret = EOF;
    unregister_stream(s);
    s->flags &= ~FLAG_OPEN;
    s->fd = -1;
    if (s->flags & FLAG_OWN_BUF) {
        free(s->buf);
        s->buf = NULL;
        s->flags &= ~FLAG_OWN_BUF;
    }
    if (s->flags & FLAG_OWN_FILE)
        free(s);
    return ret;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || ptr == NULL) {
        if (s != NULL)
            s->error = 1;
        errno = EINVAL;
        return 0;
    }
    if (size == 0 || nmemb == 0)
        return 0;
    if (nmemb > (size_t)-1 / size) {
        errno = EINVAL;
        return 0;
    }
    if (!to_read(s))
        return 0;

    size_t total = size * nmemb;
    unsigned char *out = (unsigned char *)ptr;
    size_t got = 0;
    while (got < total) {
        if (s->ungot != -1) {
            out[got++] = (unsigned char)s->ungot;
            s->ungot = -1;
            s->io_started = 1;
            continue;
        }
        if (s->pos < s->len) {
            size_t avail = s->len - s->pos;
            size_t take = total - got;
            if (take > avail)
                take = avail;
            memcpy(out + got, s->buf + s->pos, take);
            s->pos += take;
            got += take;
            s->io_started = 1;
            continue;
        }
        if (s->buf == NULL || s->mode == _IONBF) {
            size_t want = total - got;
            if (want > RAW_READ_CHUNK)
                want = RAW_READ_CHUNK;
            ssize_t n = read(s->fd, out + got, want);
            s->io_started = 1;
            if (n < 0) {
                s->error = 1;
                break;
            }
            if (n == 0) {
                s->eof = 1;
                break;
            }
            got += (size_t)n;
            continue;
        }
        int n = fill_read(s);
        s->io_started = 1;
        if (n <= 0)
            break;
    }
    return got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || ptr == NULL) {
        if (s != NULL)
            s->error = 1;
        errno = EINVAL;
        return 0;
    }
    if (size == 0 || nmemb == 0)
        return 0;
    if (nmemb > (size_t)-1 / size) {
        errno = EINVAL;
        return 0;
    }
    if (!to_write(s))
        return 0;

    size_t total = size * nmemb;
    const unsigned char *in = (const unsigned char *)ptr;
    size_t put = 0;
    while (put < total) {
        if (s->mode == _IONBF || s->buf == NULL) {
            if (s->append && lseek(s->fd, 0, SEEK_END) < 0) {
                s->error = 1;
                break;
            }
            size_t want = total - put;
            size_t done = raw_write_all(s->fd, in + put, want);
            s->io_started = 1;
            put += done;
            if (done < want) {
                s->error = 1;
                break;
            }
            continue;
        }
        size_t space = s->bufsize - s->pos;
        if (space == 0) {
            if (flush_write(s) != 0)
                break;
            space = s->bufsize;
        }
        size_t take = total - put;
        if (take > space)
            take = space;
        memcpy(s->buf + s->pos, in + put, take);
        s->pos += take;
        put += take;
        s->io_started = 1;
        if (s->pos >= s->bufsize) {
            if (flush_write(s) != 0)
                break;
        } else if (s->mode == _IOLBF) {
            int newline = 0;
            for (size_t k = 0; k < take; k++) {
                if (in[put - take + k] == '\n') {
                    newline = 1;
                    break;
                }
            }
            if (newline && flush_write(s) != 0)
                break;
        }
    }
    return put / size;
}

int fgetc(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL) {
        errno = EINVAL;
        return EOF;
    }
    if (!to_read(s))
        return EOF;
    if (s->ungot != -1) {
        int c = s->ungot;
        s->ungot = -1;
        s->io_started = 1;
        return c;
    }
    if (s->buf == NULL || s->mode == _IONBF) {
        unsigned char ch;
        ssize_t n = read(s->fd, &ch, 1);
        s->io_started = 1;
        if (n < 0) {
            s->error = 1;
            return EOF;
        }
        if (n == 0) {
            s->eof = 1;
            return EOF;
        }
        return (int)ch;
    }
    if (s->pos >= s->len) {
        int n = fill_read(s);
        s->io_started = 1;
        if (n <= 0)
            return EOF;
    } else {
        s->io_started = 1;
    }
    return (int)s->buf[s->pos++];
}

int getc(FILE *stream) {
    return fgetc(stream);
}

char *fgets(char *buf, int n, FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || buf == NULL || n <= 0) {
        if (s != NULL)
            s->error = 1;
        errno = EINVAL;
        return NULL;
    }
    if (!to_read(s))
        return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(s);
        if (c == EOF)
            break;
        buf[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return NULL;
    buf[i] = 0;
    return buf;
}

int ungetc(int c, FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || c == EOF)
        return EOF;
    if (!(s->flags & FLAG_OPEN) || !(s->flags & FLAG_READABLE))
        return EOF;
    if (s->ungot != -1)
        return EOF; /* single-byte pushback depth */
    if (s->dir == DIR_WRITE && flush_write(s) != 0)
        return EOF;
    s->dir = DIR_READ;
    s->ungot = (unsigned char)c;
    s->eof = 0;
    return (unsigned char)c;
}

int fputc(int c, FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL) {
        errno = EINVAL;
        return EOF;
    }
    if (!to_write(s))
        return EOF;
    unsigned char ch = (unsigned char)c;
    if (s->mode == _IONBF || s->buf == NULL) {
        if (s->append && lseek(s->fd, 0, SEEK_END) < 0) {
            s->error = 1;
            return EOF;
        }
        if (raw_write_all(s->fd, &ch, 1) != 1) {
            s->error = 1;
            return EOF;
        }
        s->io_started = 1;
        return (int)ch;
    }
    s->buf[s->pos++] = ch;
    s->io_started = 1;
    if (s->pos >= s->bufsize) {
        if (flush_write(s) != 0)
            return EOF;
    } else if (s->mode == _IOLBF && ch == '\n') {
        if (flush_write(s) != 0)
            return EOF;
    }
    return (int)ch;
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

int fputs(const char *str, FILE *stream) {
    if (str == NULL) {
        errno = EINVAL;
        return EOF;
    }
    size_t len = strlen(str);
    if (len == 0)
        return 0;
    if (fwrite(str, 1, len, stream) != len)
        return EOF;
    return 0;
}

static void flush_all_writable(void) {
    (void)flush_write(&g_stdout);
    (void)flush_write(&g_stderr);
    for (FILE_impl *s = g_open_streams; s != NULL; s = s->next_open) {
        if ((s->flags & FLAG_OPEN) && (s->flags & FLAG_WRITABLE) && s->dir == DIR_WRITE)
            (void)flush_write(s);
    }
}

/* Called from the libc exit path (see exit() in syscall.c) so buffered writes
 * are flushed when a program returns from main or calls exit without fclose. */
void __bigos_stdio_cleanup(void) {
    flush_all_writable();
}

int fflush(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL) {
        flush_all_writable();
        return 0;
    }
    if (!(s->flags & FLAG_OPEN)) {
        errno = EBADF;
        return EOF;
    }
    if (s->dir == DIR_WRITE)
        return flush_write(s);
    return 0;
}

int feof(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL)
        return 0;
    return s->eof != 0;
}

int ferror(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL)
        return 0;
    return s->error != 0;
}

void clearerr(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL)
        return;
    s->eof = 0;
    s->error = 0;
}

int fileno(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || !(s->flags & FLAG_OPEN)) {
        errno = EBADF;
        return -1;
    }
    return s->fd;
}

long ftell(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || !(s->flags & FLAG_OPEN)) {
        errno = EBADF;
        return -1;
    }
    off_t base = lseek(s->fd, 0, SEEK_CUR);
    if (base < 0)
        return -1;
    long position = (long)base;
    if (s->dir == DIR_WRITE) {
        position += (long)s->pos;
    } else if (s->dir == DIR_READ) {
        position -= (long)(s->len - s->pos);
        if (s->ungot != -1)
            position -= 1;
    }
    return position;
}

int fseek(FILE *stream, long offset, int whence) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || !(s->flags & FLAG_OPEN)) {
        errno = EBADF;
        return -1;
    }
    if (s->dir == DIR_WRITE && flush_write(s) != 0)
        return -1;

    long target;
    if (whence == SEEK_SET) {
        target = offset;
    } else if (whence == SEEK_CUR) {
        long cur = ftell(s);
        if (cur < 0)
            return -1;
        target = cur + offset;
    } else if (whence == SEEK_END) {
        off_t end = lseek(s->fd, 0, SEEK_END);
        if (end < 0)
            return -1;
        target = (long)end + offset;
    } else {
        errno = EINVAL;
        return -1;
    }

    s->pos = 0;
    s->len = 0;
    s->ungot = -1;
    if (lseek(s->fd, (off_t)target, SEEK_SET) < 0)
        return -1;
    s->eof = 0;
    s->dir = DIR_NONE;
    return 0;
}

void rewind(FILE *stream) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL)
        return;
    (void)fseek(stream, 0, SEEK_SET);
    s->error = 0;
    s->eof = 0;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL || !(s->flags & FLAG_OPEN)) {
        errno = EBADF;
        return -1;
    }
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
        errno = EINVAL;
        return -1;
    }
    if (s->io_started) {
        errno = EINVAL;
        return -1;
    }
    if (s->flags & FLAG_OWN_BUF) {
        free(s->buf);
        s->buf = NULL;
        s->flags &= ~FLAG_OWN_BUF;
    }
    if (mode == _IONBF || size == 0) {
        s->buf = NULL;
        s->bufsize = 0;
        s->mode = _IONBF;
        s->pos = 0;
        s->len = 0;
        return 0;
    }
    if (buf != NULL) {
        s->buf = (unsigned char *)buf; /* caller-owned: not freed by libc */
        s->bufsize = size;
    } else {
        unsigned char *nb = (unsigned char *)malloc(size);
        if (nb == NULL) {
            errno = ENOMEM;
            return -1;
        }
        s->buf = nb;
        s->bufsize = size;
        s->flags |= FLAG_OWN_BUF;
    }
    s->mode = mode;
    s->pos = 0;
    s->len = 0;
    return 0;
}

void setbuf(FILE *stream, char *buf) {
    if (buf != NULL)
        (void)setvbuf(stream, buf, _IOFBF, BUFSIZ);
    else
        (void)setvbuf(stream, NULL, _IONBF, 0);
}

/* ---- Shared bounded formatter (stdout/stderr/string) ---- */

struct format_sink {
    FILE_impl *stream; /* non-NULL: write via fputc */
    char *buf;         /* non-NULL: snprintf buffer */
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
    } else if (sink->stream != NULL) {
        if (fputc((unsigned char)c, (FILE *)sink->stream) == EOF) {
            sink->failed = 1;
            return;
        }
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

int printf(const char *fmt, ...) {
    struct format_sink sink = { .stream = &g_stdout, .buf = NULL, .size = 0, .total = 0, .failed = 0 };
    va_list ap;
    va_start(ap, fmt);
    int count = format_core(&sink, fmt, ap);
    va_end(ap);
    return count;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    FILE_impl *s = (FILE_impl *)stream;
    if (s == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct format_sink sink = { .stream = s, .buf = NULL, .size = 0, .total = 0, .failed = 0 };
    va_list ap;
    va_start(ap, fmt);
    int count = format_core(&sink, fmt, ap);
    va_end(ap);
    return count;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    if (buf == NULL && size != 0) {
        errno = EINVAL;
        return -1;
    }
    struct format_sink sink = { .stream = NULL, .buf = buf, .size = size, .total = 0, .failed = 0 };
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

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char *s) {
    if (fputs(s, stdout) == EOF)
        return EOF;
    if (fputc('\n', stdout) == EOF)
        return EOF;
    return 0;
}

void perror(const char *s) {
    struct format_sink sink = { .stream = &g_stderr, .buf = NULL, .size = 0, .total = 0, .failed = 0 };
    if (s != NULL && s[0] != 0) {
        format_string(&sink, s, 0);
        format_string(&sink, ": ", 0);
    }
    format_string(&sink, strerror(errno), 0);
    sink_char(&sink, '\n');
}
