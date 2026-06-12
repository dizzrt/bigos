/* Smoke environment probe: reports the bounded envp/getenv view. */
#include "libc.h"

static void record(const char *s) {
    int fd = open("/rw/smoke_env.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    write(fd, s, strlen(s));
    close(fd);
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;

    int count = 0;
    if (envp != NULL) {
        while (envp[count] != NULL)
            count++;
    }

    if (count == 0) {
        printf("smoke_env boundary=empty\n");
        record("smoke_env boundary=empty\n");
    } else {
        printf("smoke_env count=%d\n", count);
        for (int i = 0; i < count; i++)
            printf("smoke_env envp[%d]=%s\n", i, envp[i]);
        record("smoke_env boundary=present\n");
    }

    char *path = getenv("PATH");
    printf("smoke_env getenv(PATH)=%s\n", path != NULL ? path : "(null)");
    return 0;
}
