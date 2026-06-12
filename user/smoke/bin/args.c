/* Smoke argument probe: prints argc/argv and returns non-zero if the
 * validation arguments are malformed. */
#include "libc.h"

static void record(const char *s) {
    int fd = open("/rw/smoke_args.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    write(fd, s, strlen(s));
    close(fd);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    printf("smoke_args argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("smoke_args argv[%d]=%s\n", i, argv[i]);

    if (argc >= 3 && strcmp(argv[1], "alpha") == 0 && strcmp(argv[2], "beta") == 0) {
        record("smoke_args argc=3 argv[2]=beta\n");
        return 0;
    }
    record("smoke_args mismatch\n");
    return 2;
}
