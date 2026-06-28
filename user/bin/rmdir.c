/* BigOS bounded empty-directory remove utility. Not recursive and not complete POSIX rmdir. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2) {
        tool_error("rmdir", "usage: rmdir PATH...");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("rmdir", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        if (rmdir(argv[i]) != 0) {
            tool_errno_error("rmdir", argv[i], "rmdir");
            rc = 1;
        }
    }
    return rc;
}
