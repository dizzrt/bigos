/* BigOS bounded path-rename utility. Not a complete POSIX mv. */
#include "tool_common.h"

static void report_error(const char *oldpath, const char *newpath) {
    tool_write_all(2, "rename: ");
    tool_write_all(2, oldpath);
    tool_write_all(2, " -> ");
    tool_write_all(2, newpath);
    tool_write_all(2, ": rename: ");
    tool_write_all(2, strerror(errno));
    tool_write_all(2, "\n");
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc != 3) {
        tool_error("rename", "usage: rename OLDPATH NEWPATH");
        return 1;
    }
    if (tool_reject_unsupported_option("rename", argv[1]) != 0 ||
        tool_reject_unsupported_option("rename", argv[2]) != 0)
        return 1;

    if (rename(argv[1], argv[2]) != 0) {
        report_error(argv[1], argv[2]);
        return 1;
    }
    return 0;
}
