/* BigOS bounded dirname. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        tool_error("dirname", "usage: dirname PATH");
        return 1;
    }
    if (tool_reject_unsupported_option("dirname", argv[1]) != 0)
        return 1;
    const char *s = argv[1];
    size_t len = strlen(s);
    while (len > 1 && s[len - 1] == '/')
        len--;
    size_t slash = len;
    while (slash > 0 && s[slash - 1] != '/')
        slash--;
    if (slash == 0) {
        tool_write_all(1, ".\n");
        return 0;
    }
    while (slash > 1 && s[slash - 1] == '/')
        slash--;
    write(1, s, slash);
    write(1, "\n", 1);
    return 0;
}
