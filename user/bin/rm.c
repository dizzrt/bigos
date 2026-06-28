/* BigOS bounded path-remove utility. Not recursive and not a complete POSIX rm. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2) {
        tool_error("rm", "usage: rm PATH...");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("rm", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        if (unlink(argv[i]) != 0) {
            tool_errno_error("rm", argv[i], "unlink");
            rc = 1;
        }
    }
    return rc;
}
