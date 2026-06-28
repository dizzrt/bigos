/* BigOS bounded basename. */
#include "tool_common.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        tool_error("basename", "usage: basename PATH");
        return 1;
    }
    if (tool_reject_unsupported_option("basename", argv[1]) != 0)
        return 1;
    const char *s = argv[1];
    size_t len = strlen(s);
    while (len > 1 && s[len - 1] == '/')
        len--;
    size_t start = len;
    while (start > 0 && s[start - 1] != '/')
        start--;
    if (len == 0)
        tool_write_all(1, ".\n");
    else {
        write(1, s + start, len - start);
        write(1, "\n", 1);
    }
    return 0;
}
