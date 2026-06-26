/* BigOS bounded head: print the first N lines, default 10. */
#include "tool_common.h"

static int head_fd(int fd, const char *path, unsigned long lines) {
    char buf[TOOL_BUF_SIZE];
    unsigned long seen = 0;
    while (seen < lines) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error("head", path, "read");
            return 1;
        }
        if (n == 0)
            return 0;
        size_t keep = 0;
        while (keep < (size_t)n && seen < lines) {
            if (buf[keep] == '\n')
                seen++;
            keep++;
        }
        size_t off = 0;
        while (off < keep) {
            ssize_t w = write(1, buf + off, keep - off);
            if (w <= 0) {
                tool_errno_error("head", path, "write");
                return 1;
            }
            off += (size_t)w;
        }
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    unsigned long lines = 10;
    int arg = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        if (tool_parse_ulong(argv[2], &lines) != 0) {
            tool_error("head", "invalid line count");
            return 1;
        }
        arg = 3;
    }
    if (arg == argc)
        return head_fd(0, NULL, lines);
    int rc = 0;
    for (int i = arg; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            tool_errno_error("head", argv[i], "open");
            rc = 1;
            continue;
        }
        if (head_fd(fd, argv[i], lines) != 0)
            rc = 1;
        close(fd);
    }
    return rc;
}
