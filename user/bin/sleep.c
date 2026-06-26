/* BigOS bounded sleep: coarse second sleep through the blocking sleep syscall. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        tool_error("sleep", "usage: sleep SECONDS");
        return 1;
    }
    unsigned long seconds = 0;
    if (tool_parse_ulong(argv[1], &seconds) != 0 || seconds > 0xfffffffful) {
        tool_error("sleep", "invalid seconds");
        return 1;
    }
    unsigned int remaining = sleep((unsigned int)seconds);
    if (remaining != 0) {
        tool_errno_error("sleep", NULL, "sleep");
        return 1;
    }
    return 0;
}
