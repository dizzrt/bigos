/* BigOS bounded file-content utility. Not a complete POSIX cat.
 *
 * With no operands it copies stdin to stdout for shell pipe/redirection smoke
 * tests. With operands it opens each path through the libc/kernel path contract
 * and writes bytes in argv order. */
#include "libc.h"

static void write_all(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            exit(1);
        s += n;
        len -= (size_t)n;
    }
}

static void write_errno_value(int e) {
    char digits[12];
    int n = 0;
    if (e == 0)
        digits[n++] = '0';
    while (e != 0) {
        digits[n++] = (char)('0' + (e % 10));
        e /= 10;
    }
    while (n > 0) {
        char ch = digits[--n];
        if (write(2, &ch, 1) != 1)
            exit(1);
    }
}

static void report_error(const char *path, const char *op) {
    write_all(2, "cat: ");
    if (path != NULL) {
        write_all(2, path);
        write_all(2, ": ");
    }
    write_all(2, op);
    write_all(2, " errno=");
    write_errno_value(errno);
    write_all(2, " ");
    write_all(2, strerror(errno));
    write_all(2, "\n");
}

static int copy_fd(int fd, const char *path) {
    char buf[128];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            report_error(path, "read");
            return 1;
        }
        if (n == 0)
            return 0;
        size_t written = 0;
        while (written < (size_t)n) {
            ssize_t w = write(1, buf + written, (size_t)n - written);
            if (w <= 0) {
                report_error(path, "write");
                return 1;
            }
            written += (size_t)w;
        }
    }
}

static int copy_path(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        report_error(path, "open");
        return 1;
    }
    int rc = copy_fd(fd, path);
    if (close(fd) != 0 && rc == 0) {
        report_error(path, "close");
        rc = 1;
    }
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2)
        return copy_fd(0, NULL);

    int rc = 0;
    for (int i = 1; i < argc; i++)
        if (copy_path(argv[i]) != 0)
            rc = 1;
    return rc;
}
