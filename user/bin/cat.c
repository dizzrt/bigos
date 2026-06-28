/* BigOS bounded file-content utility. Not a complete POSIX cat.
 *
 * With no operands it copies stdin to stdout for shell pipe/redirection smoke
 * tests. With operands it opens each path through the libc/kernel path contract
 * and writes bytes in argv order. */
#include "tool_common.h"

static int copy_fd(int fd, const char *path) {
    char buf[128];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error("cat", path, "read");
            return 1;
        }
        if (n == 0)
            return 0;
        size_t written = 0;
        while (written < (size_t)n) {
            ssize_t w = write(1, buf + written, (size_t)n - written);
            if (w <= 0) {
                tool_errno_error("cat", path, "write");
                return 1;
            }
            written += (size_t)w;
        }
    }
}

static int copy_path(const char *path) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        tool_errno_error("cat", path, "open");
        return 1;
    }
    int rc = copy_fd(fd, path);
    if (close(fd) != 0 && rc == 0) {
        tool_errno_error("cat", path, "close");
        rc = 1;
    }
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2)
        return copy_fd(0, NULL);

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("cat", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        if (copy_path(argv[i]) != 0)
            rc = 1;
    }
    return rc;
}
