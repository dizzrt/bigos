#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: truncate SIZE PATH\n");
        return 1;
    }
    off_t size = 0;
    if (parse_size(argv[1], &size) != 0) {
        printf("truncate: invalid size\n");
        return 1;
    }
    if (truncate(argv[2], size) != 0) {
        printf("truncate: failed errno=%d\n", errno);
        return 1;
    }
    return 0;
}
