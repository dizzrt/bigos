/* Smoke errno probe: exercises a deterministic failing libc wrapper. */
#include "libc.h"

static void write_all(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            return;
        s += n;
        len -= (size_t)n;
    }
}

static void record(const char *s) {
    int fd = open("/rw/smoke_errno.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    write_all(fd, s);
    close(fd);
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    errno = 0;
    int fd = open("/smoke/missing", O_RDONLY, 0);
    if (fd == -1 && errno == ENOENT) {
        write_all(2, "smoke_errno open=-1 errno=2\n");
        record("smoke_errno open=-1 errno=2\n");
        return 0;
    }

    printf("smoke_errno unexpected fd=%d errno=%d\n", fd, errno);
    record("smoke_errno mismatch\n");
    if (fd >= 0)
        close(fd);
    return 1;
}
