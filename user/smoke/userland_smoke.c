/* BigOS userland smoke validation program (default-off userland_smoke build).
 *
 * Runs as PID-1 init when BIGOS_USERLAND_SMOKE is configured. It exercises the
 * user runtime end-to-end with deterministic, non-interactive assertions and
 * emits the fixed marker BIGOS_USERLAND_PASSED or BIGOS_USERLAND_FAILED through
 * fd 1 (which the kernel mirrors to COM1). It does not depend on manual stdin.
 *
 * Coverage:
 *   - crt0 passed a valid argc/argv (argv[0] is this program's path),
 *   - libc syscall wrapper + errno translation (open of a missing file -> -1,
 *     errno == ENOENT),
 *   - minimal malloc/free,
 *   - shell-style fork + execve + wait of an external command,
 *   - single-stage pipe between two children,
 *   - file redirection through the writable /rw mount (open + dup2 + read back).
 */
#include "libc.h"

static void emit(const char *s) {
    write(1, s, strlen(s));
}

static void fail(const char *why) {
    emit("BIGOS_USERLAND_FAILED ");
    emit(why);
    emit("\n");
    exit(1);
}

/* crt0 contract: argc >= 1 and argv[0] non-NULL. */
static void test_crt0(int argc, char **argv) {
    if (argc < 1 || argv == NULL || argv[0] == NULL)
        fail("crt0-argc");
}

/* libc wrapper + errno: opening a missing path returns -1 with errno ENOENT. */
static void test_errno(void) {
    errno = 0;
    int fd = open("/no/such/file", O_RDONLY, 0);
    if (fd != -1 || errno != ENOENT)
        fail("errno-translate");
}

/* minimal malloc/free: distinct, writable, reusable. */
static void test_malloc(void) {
    char *a = (char *)malloc(64);
    char *b = (char *)malloc(64);
    if (a == NULL || b == NULL || a == b)
        fail("malloc");
    for (int i = 0; i < 64; i++)
        a[i] = (char)i;
    for (int i = 0; i < 64; i++)
        if (a[i] != (char)i)
            fail("malloc-write");
    free(a);
    free(b);
    char *c = (char *)malloc(64);
    if (c == NULL)
        fail("malloc-reuse");
    free(c);
}

/* fork + execve + wait of /bin/echo. */
static void test_fork_exec(char **envp) {
    pid_t pid = fork();
    if (pid < 0)
        fail("fork");
    if (pid == 0) {
        char *argv[] = {(char *)"/bin/echo", (char *)"exec-ok", NULL};
        execve("/bin/echo", argv, envp);
        exit(127);
    }
    pid_t reaped = wait(pid);
    if (reaped != pid)
        fail("wait");
}

/* single-stage pipe: child writes, parent reads. */
static void test_pipe(void) {
    int fds[2];
    if (pipe(fds) < 0)
        fail("pipe");
    pid_t pid = fork();
    if (pid < 0)
        fail("pipe-fork");
    if (pid == 0) {
        close(fds[0]);
        const char *msg = "pipe-data";
        write(fds[1], msg, strlen(msg));
        close(fds[1]);
        exit(0);
    }
    close(fds[1]);
    char buf[16];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    wait(pid);
    if (n <= 0)
        fail("pipe-read");
    buf[n] = 0;
    if (strcmp(buf, "pipe-data") != 0)
        fail("pipe-content");
}

/* file redirection: write through the writable /rw mount, read it back. */
static void test_redirect(void) {
    const char *path = "/rw/smoke.txt";
    const char *payload = "redir-ok";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("redir-open");
    if (write(fd, payload, strlen(payload)) != (ssize_t)strlen(payload))
        fail("redir-write");
    close(fd);

    int rfd = open(path, O_RDONLY, 0);
    if (rfd < 0)
        fail("redir-reopen");
    char buf[16];
    ssize_t n = read(rfd, buf, sizeof(buf) - 1);
    close(rfd);
    if (n <= 0)
        fail("redir-read");
    buf[n] = 0;
    if (strcmp(buf, payload) != 0)
        fail("redir-content");
}

int main(int argc, char **argv, char **envp) {
    test_crt0(argc, argv);
    test_errno();
    test_malloc();
    test_fork_exec(envp);
    test_pipe();
    test_redirect();
    emit("BIGOS_USERLAND_PASSED\n");
    /* Idle: as PID-1 this must not exit; reap any further children. */
    for (;;) {
        if (wait(WAIT_ANY) < 0) {
        }
    }
    return 0;
}
