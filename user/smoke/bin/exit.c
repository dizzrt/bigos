/* Smoke exit-code probe: returns the requested small integer status. */
#include "libc.h"

static int parse_status(const char *s) {
    int value = 0;
    int neg = 0;
    if (s != NULL && *s == '-') {
        neg = 1;
        s++;
    }
    while (s != NULL && *s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return neg ? -value : value;
}

static void record(void) {
    int fd = open("/rw/smoke_exit.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    const char *msg = "smoke_exit requested=7\n";
    write(fd, msg, strlen(msg));
    close(fd);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;

    int status = argc > 1 ? parse_status(argv[1]) : 0;
    printf("smoke_exit requested=%d\n", status);
    if (status == 7)
        record();
    return status;
}
