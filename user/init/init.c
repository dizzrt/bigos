/* BigOS PID-1 init: resident C init linked against crt0 and the user libc.
 *
 * Starts /bin/sh via fork + execve, then loops in wait() reaping exited children
 * (including orphans reparented to PID-1). When /bin/sh exits it is relaunched.
 * init itself never exits. fork/execve/wait failures are reported deterministically
 * and routed through the existing reaper / BIGOS_INIT_* boundaries. */
#include "libc.h"

static const char *SHELL_PATH = "/bin/sh";

static pid_t spawn_shell(char **envp) {
    pid_t pid = fork();
    if (pid < 0) {
        printf("init: fork failed\n");
        return -1;
    }
    if (pid == 0) {
        char *argv[] = {(char *)SHELL_PATH, NULL};
        execve(SHELL_PATH, argv, envp);
        printf("init: execve %s failed\n", SHELL_PATH);
        exit(127);
    }
    return pid;
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;

    pid_t shell_pid = spawn_shell(envp);

    for (;;) {
        pid_t reaped = wait(WAIT_ANY);
        if (reaped < 0) {
            /* No children to wait for (transient); keep the shell alive. */
            if (shell_pid < 0)
                shell_pid = spawn_shell(envp);
            continue;
        }
        if (reaped == shell_pid)
            shell_pid = spawn_shell(envp);
    }
    return 0;
}
