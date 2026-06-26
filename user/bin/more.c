/* BigOS bounded more: simple pager for the default terminal. */
#include "tool_common.h"

#define MORE_PAGE_LINES 24

static void restore_canonical(void) {
    struct bigos_terminal_mode mode;
    mode.size = sizeof(mode);
    mode.version = BIGOS_TERMINAL_MODE_ABI_VERSION;
    mode.mode = BIGOS_TERMINAL_MODE_CANONICAL;
    mode.flags = BIGOS_TERMINAL_MODE_FLAG_NONE;
    (void)bigos_tcsetmode(&mode);
}

static int enter_raw(void) {
    struct bigos_terminal_mode mode;
    mode.size = sizeof(mode);
    mode.version = BIGOS_TERMINAL_MODE_ABI_VERSION;
    mode.mode = BIGOS_TERMINAL_MODE_RAW;
    mode.flags = BIGOS_TERMINAL_MODE_FLAG_NONE;
    return bigos_tcsetmode(&mode);
}

static int prompt_more(void) {
    tool_write_all(1, "--More--");
    char ch = 0;
    ssize_t n = read(0, &ch, 1);
    tool_write_all(1, "\r        \r");
    if (n <= 0)
        return 0;
    return ch == 'q' || ch == 'Q' ? 0 : 1;
}

static int more_fd(int fd, const char *path) {
    int raw = enter_raw() == 0;
    char buf[TOOL_BUF_SIZE];
    unsigned int lines = 0;
    int rc = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error("more", path, "read");
            rc = 1;
            break;
        }
        if (n == 0)
            break;
        for (ssize_t i = 0; i < n; i++) {
            if (write(1, &buf[i], 1) != 1) {
                tool_errno_error("more", path, "write");
                rc = 1;
                break;
            }
            if (buf[i] == '\n') {
                lines++;
                if (lines >= MORE_PAGE_LINES) {
                    lines = 0;
                    if (!prompt_more())
                        goto out;
                }
            }
        }
        if (rc != 0)
            break;
    }
out:
    if (raw)
        restore_canonical();
    return rc;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 1)
        return more_fd(0, NULL);
    if (argc != 2) {
        tool_error("more", "usage: more [PATH]");
        return 1;
    }
    int fd = open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        tool_errno_error("more", argv[1], "open");
        return 1;
    }
    int rc = more_fd(fd, argv[1]);
    close(fd);
    return rc;
}
