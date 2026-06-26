/* BigOS bounded grep: plain substring matching only, no regex. */
#include "tool_common.h"

#define GREP_LINE_MAX 512

static int grep_fd(int fd, const char *path, const char *needle) {
    char line[GREP_LINE_MAX];
    size_t len = 0;
    int matched = 0;
    int truncated = 0;
    line[0] = 0;
    for (;;) {
        char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n < 0) {
            tool_errno_error("grep", path, "read");
            return 2;
        }
        if (n == 0) {
            if (len > 0 && !truncated && strstr(line, needle) != NULL) {
                write(1, line, len);
                write(1, "\n", 1);
                matched = 1;
            }
            return matched ? 0 : 1;
        }
        if (ch == '\n') {
            if (!truncated && strstr(line, needle) != NULL) {
                write(1, line, len);
                write(1, "\n", 1);
                matched = 1;
            }
            len = 0;
            truncated = 0;
            line[0] = 0;
            continue;
        }
        if (len + 1 < sizeof(line)) {
            line[len++] = ch;
            line[len] = 0;
        } else {
            truncated = 1;
        }
    }
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) {
        tool_error("grep", "usage: grep NEEDLE [PATH...]");
        return 2;
    }
    if (argc == 2)
        return grep_fd(0, NULL, argv[1]);
    int any = 0;
    int hard_error = 0;
    for (int i = 2; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            tool_errno_error("grep", argv[i], "open");
            hard_error = 1;
            continue;
        }
        int rc = grep_fd(fd, argv[i], argv[1]);
        if (rc == 0)
            any = 1;
        else if (rc > 1)
            hard_error = 1;
        close(fd);
    }
    if (hard_error)
        return 2;
    return any ? 0 : 1;
}
