/* BigOS bounded tee: copy stdin to stdout and to each output file. */
#include "tool_common.h"

#define TEE_MAX_FILES 8

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int fds[TEE_MAX_FILES];
    int files = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("tee", argv[i]) != 0) {
            for (int j = 0; j < files; j++)
                close(fds[j]);
            return 1;
        }
        if (files >= TEE_MAX_FILES) {
            tool_error("tee", "too many output files");
            return 1;
        }
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            tool_errno_error("tee", argv[i], "open");
            for (int j = 0; j < files; j++)
                close(fds[j]);
            return 1;
        }
        fds[files++] = fd;
    }

    int rc = 0;
    char buf[TOOL_BUF_SIZE];
    for (;;) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error("tee", NULL, "read");
            rc = 1;
            break;
        }
        if (n == 0)
            break;
        size_t off = 0;
        while (off < (size_t)n) {
            ssize_t w = write(1, buf + off, (size_t)n - off);
            if (w <= 0) {
                tool_errno_error("tee", NULL, "stdout write");
                rc = 1;
                break;
            }
            off += (size_t)w;
        }
        for (int i = 0; i < files; i++) {
            off = 0;
            while (off < (size_t)n) {
                ssize_t w = write(fds[i], buf + off, (size_t)n - off);
                if (w <= 0) {
                    tool_errno_error("tee", argv[i + 1], "write");
                    rc = 1;
                    break;
                }
                off += (size_t)w;
            }
        }
        if (rc != 0)
            break;
    }

    for (int i = 0; i < files; i++)
        if (close(fds[i]) != 0 && rc == 0)
            rc = 1;
    return rc;
}
