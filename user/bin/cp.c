/* BigOS bounded cp: copy one regular file to another. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3) {
        tool_error("cp", "usage: cp SRC DST");
        return 1;
    }

    int in_fd = open(argv[1], O_RDONLY, 0);
    if (in_fd < 0) {
        tool_errno_error("cp", argv[1], "open");
        return 1;
    }

    int out_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        tool_errno_error("cp", argv[2], "open");
        close(in_fd);
        return 1;
    }

    int rc = tool_copy_fd(in_fd, out_fd, "cp", argv[1]);
    if (close(in_fd) != 0 && rc == 0) {
        tool_errno_error("cp", argv[1], "close");
        rc = 1;
    }
    if (close(out_fd) != 0 && rc == 0) {
        tool_errno_error("cp", argv[2], "close");
        rc = 1;
    }
    return rc;
}
