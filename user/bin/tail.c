/* BigOS bounded tail: print the last N lines seen in a fixed byte window. */
#include "tool_common.h"

#define TAIL_WINDOW 4096

static int tail_fd(int fd, const char *path, unsigned long lines) {
    char buf[TAIL_WINDOW];
    size_t used = 0;
    for (;;) {
        char tmp[TOOL_BUF_SIZE];
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            tool_errno_error("tail", path, "read");
            return 1;
        }
        if (n == 0)
            break;
        if ((size_t)n >= sizeof(buf)) {
            memcpy(buf, tmp + (size_t)n - sizeof(buf), sizeof(buf));
            used = sizeof(buf);
        } else {
            if (used + (size_t)n > sizeof(buf)) {
                size_t drop = used + (size_t)n - sizeof(buf);
                memmove(buf, buf + drop, used - drop);
                used -= drop;
            }
            memcpy(buf + used, tmp, (size_t)n);
            used += (size_t)n;
        }
    }

    size_t start = 0;
    unsigned long count = 0;
    for (size_t i = used; i > 0; i--) {
        if (buf[i - 1] == '\n' && i - 1 != used - 1) {
            count++;
            if (count >= lines) {
                start = i;
                break;
            }
        }
    }
    size_t off = start;
    while (off < used) {
        ssize_t w = write(1, buf + off, used - off);
        if (w <= 0) {
            tool_errno_error("tail", path, "write");
            return 1;
        }
        off += (size_t)w;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    unsigned long lines = 10;
    int arg = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        if (tool_parse_ulong(argv[2], &lines) != 0) {
            tool_error("tail", "invalid line count");
            return 1;
        }
        arg = 3;
    }
    if (arg == argc)
        return tail_fd(0, NULL, lines);
    int rc = 0;
    for (int i = arg; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            tool_errno_error("tail", argv[i], "open");
            rc = 1;
            continue;
        }
        if (tail_fd(fd, argv[i], lines) != 0)
            rc = 1;
        close(fd);
    }
    return rc;
}
