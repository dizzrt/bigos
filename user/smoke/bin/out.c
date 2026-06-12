/* Smoke output probe: writes deterministic text to stdout and stderr. */
#include "libc.h"

static int write_all(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            return -1;
        s += n;
        len -= (size_t)n;
    }
    return 0;
}

static void record(void) {
    int fd = open("/rw/smoke_out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    write_all(fd, "smoke_out stdout stderr\n");
    close(fd);
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    int ok = 1;
    ok = ok && write_all(1, "smoke_out stdout\n") == 0;
    ok = ok && write_all(2, "smoke_out stderr\n") == 0;
    record();
    return ok ? 0 : 1;
}
