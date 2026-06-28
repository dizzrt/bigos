/* BigOS bounded directory-create utility. Not a complete POSIX mkdir. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2) {
        tool_error("mkdir", "usage: mkdir PATH...");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("mkdir", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        if (mkdir(argv[i], 0755) != 0) {
            tool_errno_error("mkdir", argv[i], "mkdir");
            rc = 1;
        }
    }
    return rc;
}
