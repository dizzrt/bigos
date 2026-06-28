#include "tool_common.h"

static int parse_size(const char *s, off_t *out) {
    if (s == NULL || *s == 0 || out == NULL)
        return -1;
    off_t value = 0;
    for (const char *p = s; *p != 0; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        const off_t digit = (off_t)(*p - '0');
        if (value > (((off_t)4096 - digit) / 10))
            return -1;
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3) {
        tool_error("truncate", "usage: truncate SIZE PATH");
        return 1;
    }
    if (tool_reject_unsupported_option("truncate", argv[1]) != 0 ||
        tool_reject_unsupported_option("truncate", argv[2]) != 0)
        return 1;
    off_t size = 0;
    if (parse_size(argv[1], &size) != 0) {
        tool_error("truncate", "invalid size");
        return 1;
    }
    if (truncate(argv[2], size) != 0) {
        tool_errno_error("truncate", argv[2], "truncate");
        return 1;
    }
    return 0;
}
