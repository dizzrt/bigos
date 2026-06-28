/* BigOS bounded append: append argv text and a newline to PATH. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 3) {
        tool_error("append", "usage: append PATH TEXT...");
        return 1;
    }
    if (tool_reject_unsupported_option("append", argv[1]) != 0)
        return 1;
    int fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        tool_errno_error("append", argv[1], "open");
        return 1;
    }
    if (lseek(fd, 0, SEEK_END) < 0) {
        tool_errno_error("append", argv[1], "seek");
        close(fd);
        return 1;
    }
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && write(fd, " ", 1) != 1)
            rc = 1;
        size_t len = strlen(argv[i]);
        if (len != 0 && write(fd, argv[i], len) != (ssize_t)len)
            rc = 1;
    }
    if (write(fd, "\n", 1) != 1)
        rc = 1;
    if (close(fd) != 0 && rc == 0)
        rc = 1;
    if (rc != 0)
        tool_errno_error("append", argv[1], "write");
    return rc;
}
