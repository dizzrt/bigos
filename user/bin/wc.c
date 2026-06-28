/* BigOS bounded wc: count lines, words, and bytes. */
#include "tool_common.h"

static int wc_fd(int fd, const char *path, unsigned long *lines, unsigned long *words, unsigned long *bytes) {
    int in_word = 0;
    char buf[TOOL_BUF_SIZE];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            tool_errno_error("wc", path, "read");
            return 1;
        }
        if (n == 0)
            return 0;
        *bytes += (unsigned long)n;
        for (ssize_t i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n')
                (*lines)++;
            if (isspace(ch)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                (*words)++;
            }
        }
    }
}

static void print_counts(unsigned long lines, unsigned long words, unsigned long bytes, const char *name) {
    if (name != NULL)
        printf("%lu %lu %lu %s\n", lines, words, bytes, name);
    else
        printf("%lu %lu %lu\n", lines, words, bytes);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 1) {
        unsigned long lines = 0, words = 0, bytes = 0;
        int rc = wc_fd(0, NULL, &lines, &words, &bytes);
        print_counts(lines, words, bytes, NULL);
        return rc;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (tool_reject_unsupported_option("wc", argv[i]) != 0) {
            rc = 1;
            continue;
        }
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            tool_errno_error("wc", argv[i], "open");
            rc = 1;
            continue;
        }
        unsigned long lines = 0, words = 0, bytes = 0;
        if (wc_fd(fd, argv[i], &lines, &words, &bytes) != 0)
            rc = 1;
        print_counts(lines, words, bytes, argv[i]);
        close(fd);
    }
    return rc;
}
