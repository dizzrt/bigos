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
 *   - shell-style fork + execve + wait of external simple C programs,
 *   - single-stage pipe between two children,
 *   - file redirection through the writable /rw mount (open + dup2 + read back).
 *   - non-interactive /bin/sh execution of /bin/smoke probes, including
 *     stdout/stderr, errno reporting, and shell continuation after non-zero exit.
 *   - the bounded libc subset probe for fine-grained headers, fprintf(stderr),
 *     string/memory semantics, read-only environment, and allocator failure.
 */
#include "libc.h"

#define CAPTURE_MAX 512

static void emit(const char *s) {
    write(1, s, strlen(s));
}

static void fail(const char *why) {
    emit("BIGOS_USERLAND_FAILED ");
    emit(why);
    emit("\n");
    exit(1);
}

static int contains(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0)
        return 1;
    for (const char *p = haystack; *p != 0; p++) {
        if (strncmp(p, needle, nlen) == 0)
            return 1;
    }
    return 0;
}

static void write_all_or_exit(int fd, const char *s) {
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0)
            exit(125);
        s += n;
        len -= (size_t)n;
    }
}

static int dup_non_stdio(int fd) {
    int temps[3] = {-1, -1, -1};
    for (int i = 0; i < 4; i++) {
        int moved = dup(fd);
        if (moved < 0)
            break;
        if (moved >= 3) {
            for (int j = 0; j < 3; j++)
                if (temps[j] >= 0)
                    close(temps[j]);
            return moved;
        }
        if (i < 3)
            temps[i] = moved;
        else
            close(moved);
    }
    for (int j = 0; j < 3; j++)
        if (temps[j] >= 0)
            close(temps[j]);
    return -1;
}

static int move_pipe_fds_from_stdio(int fds[2]) {
    int moved[2] = {fds[0], fds[1]};
    for (int i = 0; i < 2; i++) {
        if (fds[i] < 3) {
            moved[i] = dup_non_stdio(fds[i]);
            if (moved[i] < 0) {
                for (int j = 0; j < i; j++)
                    if (moved[j] != fds[j])
                        close(moved[j]);
                return -1;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        if (moved[i] != fds[i]) {
            close(fds[i]);
            fds[i] = moved[i];
        }
    }
    return 0;
}

static void run_program(const char *path, char **child_argv, char **envp) {
    pid_t pid = fork();
    if (pid < 0)
        fail("program-fork");
    if (pid == 0) {
        execve(path, child_argv, envp);
        exit(127);
    }

    if (wait(pid) != pid)
        fail("program-wait");
}

static void require_file_contains(const char *path, const char *expect) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        fail("record-open");
    char buf[CAPTURE_MAX];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        fail("record-read");
    buf[n] = 0;
    if (!contains(buf, expect))
        fail("record-content");
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

static void test_smoke_programs(char **envp) {
    char *args_argv[] = {(char *)"/bin/smoke/args", (char *)"alpha", (char *)"beta", NULL};
    run_program("/bin/smoke/args", args_argv, envp);
    require_file_contains("/rw/smoke_args.txt", "smoke_args argc=3 argv[2]=beta");

    char *env_argv[] = {(char *)"/bin/smoke/env", NULL};
    run_program("/bin/smoke/env", env_argv, envp);
    require_file_contains("/rw/smoke_env.txt", "smoke_env boundary=");

    char *out_argv[] = {(char *)"/bin/smoke/out", NULL};
    run_program("/bin/smoke/out", out_argv, envp);
    require_file_contains("/rw/smoke_out.txt", "smoke_out stdout stderr");

    char *errno_argv[] = {(char *)"/bin/smoke/errno", NULL};
    run_program("/bin/smoke/errno", errno_argv, envp);
    require_file_contains("/rw/smoke_errno.txt", "smoke_errno open=-1 errno=2");

    char *exit_argv[] = {(char *)"/bin/smoke/exit", (char *)"7", NULL};
    run_program("/bin/smoke/exit", exit_argv, envp);
    require_file_contains("/rw/smoke_exit.txt", "smoke_exit requested=7");

    char *libc_argv[] = {(char *)"/bin/smoke/libc_subset", (char *)"alpha", NULL};
    run_program("/bin/smoke/libc_subset", libc_argv, envp);
    require_file_contains("/rw/smoke_libc_subset.txt", "smoke_libc_subset ok");
}

static void test_smoke_shell(char **envp) {
    int input[2];
    if (pipe(input) < 0)
        fail("shell-pipe");
    if (move_pipe_fds_from_stdio(input) < 0)
        fail("shell-pipe-fds");
    unlink("/rw/smoke_args.txt");

    pid_t pid = fork();
    if (pid < 0)
        fail("shell-fork");
    if (pid == 0) {
        close(input[1]);
        dup2(input[0], 0);
        close(input[0]);
        char *argv[] = {(char *)"/bin/sh", NULL};
        execve("/bin/sh", argv, envp);
        exit(127);
    }

    close(input[0]);
    write_all_or_exit(input[1], "/bin/smoke/exit 7\n");
    write_all_or_exit(input[1], "/bin/smoke/args alpha beta\n");
    write_all_or_exit(input[1], "echo shell-alive\n");
    write_all_or_exit(input[1], "exit 0\n");
    close(input[1]);

    if (wait(pid) != pid)
        fail("shell-wait");
    require_file_contains("/rw/smoke_exit.txt", "smoke_exit requested=7");
    require_file_contains("/rw/smoke_args.txt", "smoke_args argc=3 argv[2]=beta");
}

int main(int argc, char **argv, char **envp) {
    test_crt0(argc, argv);
    test_errno();
    test_malloc();
    test_fork_exec(envp);
    test_pipe();
    test_redirect();
    test_smoke_programs(envp);
    test_smoke_shell(envp);
    emit("BIGOS_USERLAND_PASSED\n");
    /* Idle: as PID-1 this must not exit; reap any further children. */
    for (;;) {
        if (wait(WAIT_ANY) < 0) {
        }
    }
    return 0;
}
