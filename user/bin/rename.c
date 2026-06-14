/* BigOS bounded path-rename utility. Not a complete POSIX mv. */
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

static void report_error(const char *oldpath, const char *newpath) {
    write_all(2, "rename: ");
    write_all(2, oldpath);
    write_all(2, " -> ");
    write_all(2, newpath);
    write_all(2, ": errno=");
    write_errno_value(errno);
    write_all(2, " ");
    write_all(2, strerror(errno));
    write_all(2, "\n");
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc != 3) {
        write_all(2, "usage: rename OLDPATH NEWPATH\n");
        return 1;
    }

    if (rename(argv[1], argv[2]) != 0) {
        report_error(argv[1], argv[2]);
        return 1;
    }
    return 0;
}
