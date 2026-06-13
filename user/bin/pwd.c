/* BigOS bounded pwd utility. Reports the kernel/libc current-directory string;
 * not a symlink-aware realpath or complete POSIX utility. */
#include "libc.h"

static void write_all(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            exit(1);
        s += n;
        len -= (size_t)n;
    }
}

static void write_errno(int e) {
    char digits[12];
    int n = 0;
    if (e == 0)
        digits[n++] = '0';
    while (e != 0) {
        digits[n++] = (char)('0' + (e % 10));
        e /= 10;
    }
    while (n > 0) {
        char ch = digits[--n];
        if (write(2, &ch, 1) != 1)
            exit(1);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    char cwd[256 + 1];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        write_all(2, "pwd: getcwd failed errno=");
        write_errno(errno);
        write_all(2, "\n");
        return 1;
    }
    write_all(1, cwd);
    write_all(1, "\n");
    return 0;
}
