/* BigOS bounded touch. Creates missing files or updates atime/mtime to now. */
#include "tool_common.h"

static int touch_one(const char *path) {
    if (utime(path, NULL) == 0)
        return 0;
    if (errno != ENOENT) {
        tool_errno_error("touch", path, "utime");
        return 1;
    }

    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        tool_errno_error("touch", path, "open");
        return 1;
    }
    if (close(fd) != 0) {
        tool_errno_error("touch", path, "close");
        return 1;
    }
    if (utime(path, NULL) != 0) {
        tool_errno_error("touch", path, "utime");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        tool_error("touch", "usage: touch PATH...");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("touch", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        if (touch_one(argv[i]) != 0)
            rc = 1;
    }
    return rc;
}
