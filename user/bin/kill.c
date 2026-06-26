/* BigOS bounded kill: send a numeric signal to a pid. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2 || argc > 3) {
        tool_error("kill", "usage: kill PID [SIGNO]");
        return 1;
    }
    unsigned long pid_value = 0;
    unsigned long signo_value = SIGTERM;
    if (tool_parse_ulong(argv[1], &pid_value) != 0 || pid_value == 0) {
        tool_error("kill", "invalid pid");
        return 1;
    }
    if (argc == 3 && (tool_parse_ulong(argv[2], &signo_value) != 0 || signo_value == 0 || signo_value > 31)) {
        tool_error("kill", "invalid signal");
        return 1;
    }
    if (kill((pid_t)pid_value, (int)signo_value) != 0) {
        tool_errno_error("kill", argv[1], "kill");
        return 1;
    }
    return 0;
}
