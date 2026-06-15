/* Smoke libc-subset probe: validates the documented bounded C library surface. */
#include "../../libc/include/errno.h"
#include "../../libc/include/bigos_dirent.h"
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
    char *zeroed = (char *)calloc(4, 8);
    if (zeroed == NULL)
        return 0;
    for (int i = 0; i < 32; i++)
        if (zeroed[i] != 0)
            return 0;
    char *grown = (char *)realloc(zeroed, 64);
    if (grown == NULL)
        return 0;
    strcpy(grown, "grow-ok");
    free(NULL);
    void *too_large = malloc((size_t)-64);
    if (too_large != NULL)
        return 0;
    void *bad_calloc = calloc((size_t)-1, 2);
    if (bad_calloc != NULL)
        return 0;
    char *kept_on_fail = (char *)realloc(grown, (size_t)-64);
    if (kept_on_fail != NULL || strcmp(grown, "grow-ok") != 0)
        return 0;
    if (strcmp(keep, "heap-ok") != 0)
        return 0;
    free(grown);
    free(keep);
    char *again = (char *)malloc(32);
    if (again == NULL)
        return 0;
    free(again);
    return 1;
}

static int check_conversions(void) {
    char *end = NULL;
    errno = EFAULT;
    long a = strtol("  -42xyz", &end, 10);
    if (a != -42 || end == NULL || strcmp(end, "xyz") != 0 || errno != EFAULT)
        return 0;
    errno = 0;
    long b = strtol("0x2a!", &end, 0);
    if (b != 42 || end == NULL || *end != '!' || errno != 0)
        return 0;
    if (atoi("123rest") != 123)
        return 0;
    errno = 0;
    (void)strtol("999999999999999999999999999999", &end, 10);
    if (errno != ERANGE)
        return 0;
    return 1;
}

static int check_snprintf(void) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "u=%u p=%p ld=%ld lu=%lu zu=%zu", 42u, buf, -7l, 9ul, (size_t)5);
    if (n <= 0 || buf[sizeof(buf) - 1] != 0)
        return 0;
    if (strncmp(buf, "u=42 p=0x", 9) != 0)
        return 0;
    char small[8];
    n = snprintf(small, sizeof(small), "%5u:%s", 7u, "abcdef");
    if (n != 12 || strcmp(small, "    7:a") != 0)
        return 0;
    return 1;
}

static int check_dir_wrapper(void) {
    errno = 0;
    DIR *missing = opendir("/smoke/libc-subset-missing-dir");
    if (missing != NULL || errno != ENOENT)
        return 0;
    errno = 0;
    DIR *dir = opendir("/");
    if (dir == NULL)
        return 0;
    int saw_entry = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL)
            break;
        if (entry->d_name[0] != 0 && (entry->d_type == BIGOS_DIRENT_TYPE_FILE || entry->d_type == BIGOS_DIRENT_TYPE_DIRECTORY))
            saw_entry = 1;
    }
    if (errno != 0)
        return 0;
    if (closedir(dir) != 0)
        return 0;
    errno = 0;
    if (closedir(NULL) != -1 || errno != EINVAL)
        return 0;
    return saw_entry;
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
    if (!check_conversions())
        return 6;
    if (!check_snprintf())
        return 7;
    if (!check_dir_wrapper())
        return 8;

    printf("smoke_libc_subset stdout argc=%d argv0=%s fmt=%8x ptr=%p size=%zu %c %%\n", argc, argv[0], 0x2a, argv, (size_t)32, 'Z');
    if (fprintf(stderr, "smoke_libc_subset stderr errno=%d env=%s width=%5u long=%ld\n", errno, getenv("PATH"), 7u, -9l) < 0)
        return 9;
    record_success();
    return 0;
}
