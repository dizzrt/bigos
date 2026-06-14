/* Smoke libc-subset probe: validates the documented bounded C library surface. */
#include "../../libc/include/errno.h"
#include "../../libc/include/fcntl.h"
#include "../../libc/include/signal.h"
#include "../../libc/include/stdio.h"
#include "../../libc/include/stdlib.h"
#include "../../libc/include/string.h"
#include "../../libc/include/sys/types.h"
#include "../../libc/include/sys/wait.h"
#include "../../libc/include/unistd.h"

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

static void record_success(void) {
    int fd = open("/rw/smoke_libc_subset.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    write_all(fd, "smoke_libc_subset ok\n");
    close(fd);
}

static int check_strings(void) {
    char buf[16] = "abcdef";
    memmove(buf + 2, buf, 4);
    if (strcmp(buf, "ababcd") != 0)
        return 0;
    memmove(buf, buf + 2, 4);
    buf[4] = 0;
    if (strcmp(buf, "abcd") != 0)
        return 0;
    if (strncmp("abcd", "abzz", 2) != 0)
        return 0;
    if (strncmp("abcd", "abzz", 3) >= 0)
        return 0;
    return 1;
}

static int check_malloc(void) {
    char *keep = (char *)malloc(32);
    if (keep == NULL || ((unsigned long)keep & 15u) != 0)
        return 0;
    strcpy(keep, "heap-ok");
    free(NULL);
    void *too_large = malloc((size_t)-64);
    if (too_large != NULL)
        return 0;
    if (strcmp(keep, "heap-ok") != 0)
        return 0;
    free(keep);
    char *again = (char *)malloc(32);
    if (again == NULL)
        return 0;
    free(again);
    return 1;
}

static int check_errno(void) {
    errno = EFAULT;
    pid_t pid = getpid();
    if (pid < 0 || errno != EFAULT)
        return 0;
    errno = 0;
    int fd = open("/smoke/libc-subset-missing", O_RDONLY, 0);
    if (fd != -1 || errno != ENOENT)
        return 0;
    return 1;
}

static int check_environment(char **envp) {
    if (environ != envp)
        return 0;
    char *path = getenv("PATH");
    if (path != NULL && strlen(path) == 0)
        return 0;
    return 1;
}

int main(int argc, char **argv, char **envp) {
    if (argc < 1 || argv == NULL || argv[0] == NULL)
        return 1;
    if (WAIT_ANY == 0)
        return 1;
    if (SIGUSR1 != 10)
        return 1;
    if (!check_environment(envp))
        return 2;
    if (!check_errno())
        return 3;
    if (!check_strings())
        return 4;
    if (!check_malloc())
        return 5;

    printf("smoke_libc_subset stdout argc=%d argv0=%s fmt=%x %c %%\n", argc, argv[0], 0x2a, 'Z');
    if (fprintf(stderr, "smoke_libc_subset stderr errno=%d env=%s\n", errno, getenv("PATH")) < 0)
        return 6;
    record_success();
    return 0;
}
