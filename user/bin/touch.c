/* BigOS bounded touch. Creates missing files or updates atime/mtime to now. */
#include "libc.h"

static void report_errno(const char *path) {
    write(2, "touch: ", 7);
    write(2, path, strlen(path));
    write(2, ": errno=", 8);
    char digits[12];
    int n = 0;
    int e = errno;
    if (e == 0)
        digits[n++] = '0';
    while (e != 0) {
        digits[n++] = (char)('0' + (e % 10));
        e /= 10;
    }
    while (n > 0) {
        char ch = digits[--n];
        write(2, &ch, 1);
    }
    write(2, "\n", 1);
}

static int touch_one(const char *path) {
    if (utime(path, NULL) == 0)
        return 0;
    if (errno != ENOENT) {
        report_errno(path);
        return 1;
    }

    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        report_errno(path);
        return 1;
    }
    if (close(fd) != 0) {
        report_errno(path);
        return 1;
    }
    if (utime(path, NULL) != 0) {
        report_errno(path);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        write(2, "usage: touch PATH...\n", 21);
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        if (touch_one(argv[i]) != 0)
            rc = 1;
    return rc;
}
