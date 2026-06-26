/* BigOS /bin/sh: minimal interactive shell.
 *
 * Read-parse-execute loop linked against crt0 and the user libc. Supports:
 *   - whitespace tokenization into a bounded argv,
 *   - builtins: exit, echo, cd, pwd, status, sync, help, env, clear, true, false,
 *   - external commands via PATH lookup + fork + execve + wait,
 *   - a single-stage pipe a | b,
 *   - basic > / < redirection.
 * All capacities are bounded and over-limit input is a deterministic error that
 * returns to the read loop instead of crashing. Foreground process groups are
 * a bounded BigOS subset only: no background jobs, fg/bg job table, termios,
 * variable expansion, globbing, or scripting control flow. */
#include "libc.h"

#define SH_MAX_LINE            256
#define SH_MAX_ARGC            32
#define SH_PATH_MAX            256
#define SH_DEFAULT_PATH        "/bin"
#define SH_READ_LINE_TOO_LONG  (-1)
#define SH_READ_LINE_EOF       (-2)
#define SH_READ_LINE_INTERRUPT (-3)

static void sh_errno_error(const char *prefix, const char *detail, int err);

static int fd_has_installed_file(int fd) {
    int dup_fd = dup(fd);
    if (dup_fd >= 0) {
        close(dup_fd);
        return 1;
    }
    return errno != EBADF;
}

static int is_interactive_session(void) {
    return !fd_has_installed_file(0) && !fd_has_installed_file(1);
}

static void install_shell_signal_policy(void) {
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SIG_IGN;
    sigaction(SIGINT, &act, NULL);
}

static void restore_child_signal_policy(void) {
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SIG_DFL;
    sigaction(SIGINT, &act, NULL);
}

static int restore_terminal_canonical(void) {
    struct bigos_terminal_mode mode = {
        sizeof(struct bigos_terminal_mode),
        BIGOS_TERMINAL_MODE_ABI_VERSION,
        BIGOS_TERMINAL_MODE_CANONICAL,
        BIGOS_TERMINAL_MODE_FLAG_NONE,
    };
    if (bigos_tcsetmode(&mode) != 0) {
        sh_errno_error("sh: terminal mode restore failed", NULL, errno);
        return -1;
    }
    return 0;
}

static void setup_shell_foreground(void) {
    (void)setsid();
    pid_t pgid = getpgrp();
    if (pgid > 0 && tcsetpgrp(0, pgid) != 0)
        sh_errno_error("sh: foreground setup failed", NULL, errno);
    (void)restore_terminal_canonical();
}

static void restore_shell_foreground(void) {
    pid_t pgid = getpgrp();
    if (pgid > 0 && tcsetpgrp(0, pgid) != 0)
        sh_errno_error("sh: foreground restore failed", NULL, errno);
    (void)restore_terminal_canonical();
}

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

static void sh_error(const char *prefix, const char *detail) {
    write_all(2, prefix);
    if (detail != NULL)
        write_all(2, detail);
    write_all(2, "\n");
}

static void sh_errno_error(const char *prefix, const char *detail, int err) {
    write_all(2, prefix);
    if (detail != NULL)
        write_all(2, detail);
    write_all(2, ": ");
    write_all(2, strerror(err));
    write_all(2, "\n");
}

static void write_int(int fd, int value) {
    char digits[12];
    int n = 0;
    unsigned int v;
    if (value < 0) {
        write_all(fd, "-");
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }
    if (v == 0)
        digits[n++] = '0';
    while (v != 0) {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        char ch = digits[--n];
        if (write(fd, &ch, 1) != 1)
            return;
    }
}

static int should_echo_input(char ch) {
    return ch == '\t' || (ch >= ' ' && ch < 0x7f);
}

static int is_unsupported_control(char ch) {
    unsigned char byte = (unsigned char)ch;
    return byte < ' ' && ch != '\t' && ch != '\n' && ch != '\b' && byte != 0x03 && byte != 0x04;
}

/* Reads one newline-terminated line from stdin into buf (bounded by cap-1).
 * Returns the length, SH_READ_LINE_EOF on EOF with no data,
 * SH_READ_LINE_INTERRUPT on the bounded interrupt-like terminal event, or
 * SH_READ_LINE_TOO_LONG when the remainder of an over-length line is drained. */
static int read_line(char *buf, int cap, int interactive) {
    int len = 0;
    for (;;) {
        char ch;
        ssize_t n = read(0, &ch, 1);
        if (n <= 0)
            return len > 0 ? len : SH_READ_LINE_EOF;
        if (ch == '\n') {
            if (interactive)
                write_all(1, "\n");
            buf[len] = 0;
            return len;
        }
        if (ch == 0x03) {
            buf[0] = 0;
            if (interactive)
                write_all(1, "^C\n");
            return SH_READ_LINE_INTERRUPT;
        }
        if (ch == 0x04)
            return len > 0 ? len : SH_READ_LINE_EOF;
        if (ch == '\b' || ch == 0x7f) {
            if (len > 0) {
                len--;
                if (interactive)
                    write_all(1, "\b");
            }
            continue;
        }
        if (is_unsupported_control(ch))
            continue;
        if (len >= cap - 1) {
            /* Drain the rest of the over-length line. */
            while (n > 0 && ch != '\n')
                n = read(0, &ch, 1);
            return SH_READ_LINE_TOO_LONG;
        }
        buf[len++] = ch;
        if (interactive && should_echo_input(ch)) {
            char echo[2] = {ch, 0};
            write_all(1, echo);
        }
    }
}

/* Splits line into argv on spaces/tabs; returns argc or -1 if over SH_MAX_ARGC.
 * argv is NULL-terminated. Modifies line in place. */
static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            *p++ = 0;
        if (*p == 0)
            break;
        if (argc >= SH_MAX_ARGC)
            return -1;
        argv[argc++] = p;
        while (*p != 0 && *p != ' ' && *p != '\t')
            p++;
    }
    argv[argc] = NULL;
    return argc;
}

static int has_slash(const char *s) {
    return strchr(s, '/') != NULL;
}

/* Resolves a command name into an absolute/relative path string in out (bounded
 * by SH_PATH_MAX). For names with '/', copies as-is. Otherwise tries each PATH
 * directory in turn, returning the first candidate that an execve would attempt;
 * here we just build candidates and let the caller execve them. Returns 1 if a
 * single direct path was produced (slash case), 0 to request PATH iteration. */
static int build_direct_path(const char *cmd, char *out) {
    if (!has_slash(cmd))
        return 0;
    size_t len = strlen(cmd);
    if (len >= SH_PATH_MAX)
        return -1;
    strcpy(out, cmd);
    return 1;
}

/* Performs execve over PATH candidates for a non-slash command. Only retries on
 * ENOENT; any other error stops. Returns only on failure (execve replaces the
 * image on success); -1 means no candidate fit within SH_PATH_MAX. */
static int exec_with_path(const char *cmd, char **argv) {
    const char *path = getenv("PATH");
    if (path == NULL || *path == 0)
        path = SH_DEFAULT_PATH;

    char candidate[SH_PATH_MAX];
    const char *dir = path;
    int attempted = 0;
    int too_long = 0;
    while (dir != NULL) {
        const char *end = strchr(dir, ':');
        size_t dir_len = end != NULL ? (size_t)(end - dir) : strlen(dir);
        size_t cmd_len = strlen(cmd);
        if (dir_len + 1 + cmd_len < SH_PATH_MAX) {
            attempted = 1;
            size_t i = 0;
            for (; i < dir_len; i++)
                candidate[i] = dir[i];
            if (dir_len == 0 || candidate[dir_len - 1] != '/')
                candidate[i++] = '/';
            strcpy(candidate + i, cmd);
            execve(candidate, argv, environ);
            if (errno != ENOENT)
                break; /* a real error, not "try next dir" */
        } else {
            too_long = 1;
        }
        dir = end != NULL ? end + 1 : NULL;
    }
    return too_long && !attempted ? -1 : 0;
}

/* Runs an external command (already tokenized argv). Forks; the child execve's
 * the resolved path; the parent waits. On execve failure the child reports and
 * exits non-zero, leaving the parent shell intact. */
static int run_external(char **argv, int in_fd, int out_fd) {
    pid_t pid = fork();
    if (pid < 0) {
        sh_error("sh: fork failed", NULL);
        restore_shell_foreground();
        return 126;
    }
    if (pid == 0) {
        restore_child_signal_policy();
        (void)setpgid(0, 0);
        if (in_fd >= 0) {
            if (dup2(in_fd, 0) < 0)
                exit(126);
            close(in_fd);
        }
        if (out_fd >= 0) {
            if (dup2(out_fd, 1) < 0)
                exit(126);
            close(out_fd);
        }
        char direct[SH_PATH_MAX];
        int rc = build_direct_path(argv[0], direct);
        if (rc == 1)
            execve(direct, argv, environ);
        else if (rc == 0)
            rc = exec_with_path(argv[0], argv);
        if (rc < 0)
            sh_error("sh: path too long: ", argv[0]);
        else if (errno == ENOEXEC || errno == EACCES)
            sh_error("sh: exec failed: ", argv[0]);
        else
            sh_error("sh: command not found: ", argv[0]);
        exit(rc < 0 ? 126 : 127);
    }
    if (setpgid(pid, pid) != 0)
        sh_errno_error("sh: setpgid failed", argv[0], errno);
    if (tcsetpgrp(0, pid) != 0)
        sh_errno_error("sh: foreground command setup failed", argv[0], errno);
    int status = 126;
    if (wait_status(pid, &status) != pid) {
        sh_error("sh: wait failed", NULL);
        restore_shell_foreground();
        return 126;
    }
    restore_shell_foreground();
    if (status != 0)
        sh_error("sh: command failed: ", argv[0]);
    return status;
}

/* Handles builtins. Returns 1 if handled, 0 otherwise. */
static int run_builtin(int argc, char **argv, int out_fd, int last_status, int *status) {
    *status = 0;
    if (strcmp(argv[0], "exit") == 0) {
        int code = last_status;
        if (argc > 1) {
            code = 0;
            const char *p = argv[1];
            int neg = 0;
            if (*p == '-') {
                neg = 1;
                p++;
            }
            while (*p >= '0' && *p <= '9')
                code = code * 10 + (*p++ - '0');
            if (neg)
                code = -code;
        }
        exit(code);
    }
    if (strcmp(argv[0], "status") == 0) {
        int fd = out_fd >= 0 ? out_fd : 1;
        write_all(fd, "status ");
        write_int(fd, last_status);
        write_all(fd, "\n");
        return 1;
    }
    if (strcmp(argv[0], "echo") == 0) {
        int fd = out_fd >= 0 ? out_fd : 1;
        for (int i = 1; i < argc; i++) {
            if (i > 1)
                write_all(fd, " ");
            size_t len = strlen(argv[i]);
            if (len != 0 && write(fd, argv[i], len) != (ssize_t)len)
                *status = 1;
        }
        write_all(fd, "\n");
        return 1;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        if (argc != 1) {
            sh_error("sh: pwd: usage: pwd", NULL);
            *status = 1;
            return 1;
        }
        char cwd[SH_PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            sh_errno_error("sh: pwd failed", NULL, errno);
            *status = 1;
            return 1;
        }
        int fd = out_fd >= 0 ? out_fd : 1;
        write_all(fd, cwd);
        write_all(fd, "\n");
        return 1;
    }
    if (strcmp(argv[0], "help") == 0) {
        if (argc != 1) {
            sh_error("sh: help: usage: help", NULL);
            *status = 1;
            return 1;
        }
        int fd = out_fd >= 0 ? out_fd : 1;
        write_all(fd, "BigOS shell builtins:\n");
        write_all(fd, "  exit [code]  echo [args...]  cd PATH  pwd  status  sync\n");
        write_all(fd, "  help  env  clear  true  false\n");
        write_all(fd, "External commands run via PATH (default /bin) using fork/execve/wait.\n");
        write_all(fd, "Supported syntax: whitespace argv, one pipe with |, input <, output >.\n");
        write_all(fd, "Unsupported: quoting, variables, globbing, scripts, jobs, background tasks.\n");
        return 1;
    }
    if (strcmp(argv[0], "env") == 0) {
        if (argc != 1) {
            sh_error("sh: env: usage: env", NULL);
            *status = 1;
            return 1;
        }
        int fd = out_fd >= 0 ? out_fd : 1;
        if (environ != NULL) {
            for (char **e = environ; *e != NULL; e++) {
                write_all(fd, *e);
                write_all(fd, "\n");
            }
        }
        return 1;
    }
    if (strcmp(argv[0], "clear") == 0) {
        if (argc != 1) {
            sh_error("sh: clear: usage: clear", NULL);
            *status = 1;
            return 1;
        }
        int fd = out_fd >= 0 ? out_fd : 1;
        write_all(fd, "\033[2J\033[H");
        return 1;
    }
    if (strcmp(argv[0], "true") == 0) {
        if (argc != 1) {
            sh_error("sh: true: usage: true", NULL);
            *status = 1;
        }
        return 1;
    }
    if (strcmp(argv[0], "false") == 0) {
        if (argc != 1)
            sh_error("sh: false: usage: false", NULL);
        *status = 1;
        return 1;
    }
    if (strcmp(argv[0], "cd") == 0) {
        if (out_fd >= 0) {
            sh_error("sh: cd does not write output", NULL);
            *status = 1;
            return 1;
        }
        if (argc != 2) {
            sh_error("sh: cd: usage: cd PATH", NULL);
            *status = 1;
            return 1;
        }
        if (chdir(argv[1]) != 0) {
            sh_errno_error("sh: cd failed: ", argv[1], errno);
            *status = 1;
        }
        return 1;
    }
    if (strcmp(argv[0], "sync") == 0) {
        if (out_fd >= 0) {
            sh_error("sh: sync does not write output", NULL);
            *status = 1;
            return 1;
        }
        if (argc != 1) {
            sh_error("sh: sync: usage: sync", NULL);
            *status = 1;
            return 1;
        }
        if (sync() != 0) {
            sh_errno_error("sh: sync failed", NULL, errno);
            *status = 1;
        }
        return 1;
    }
    return 0;
}

static int close_redirects(int in_fd, int out_fd) {
    int rc = 0;
    if (in_fd >= 0 && close(in_fd) != 0)
        rc = -1;
    if (out_fd >= 0 && close(out_fd) != 0)
        rc = -1;
    return rc;
}

static int contains_unsupported_token(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], ">>") == 0 || strcmp(argv[i], "2>") == 0 || strcmp(argv[i], "||") == 0 ||
            strcmp(argv[i], "&&") == 0 || strcmp(argv[i], ";") == 0 || strcmp(argv[i], "&") == 0) {
            sh_error("sh: unsupported syntax: ", argv[i]);
            return 1;
        }
    }
    if (argc > 0 && (strcmp(argv[0], "fg") == 0 || strcmp(argv[0], "bg") == 0)) {
        sh_error("sh: unsupported job control: ", argv[0]);
        return 1;
    }
    return 0;
}

static int contains_redirection_token(int argc, char **argv) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], "<") == 0)
            return 1;
    return 0;
}

static int move_fd_from_stdio(int fd);

/* Parses and strips redirections (> file, < file) from argv, opening the files
 * and returning fds via in_fd/out_fd (or -1). Returns 0 on success, -1 on a
 * deterministic error (missing filename or open failure). */
static int apply_redirects(char **argv, int *argc, int *in_fd, int *out_fd) {
    *in_fd = -1;
    *out_fd = -1;
    int w = 0;
    for (int r = 0; r < *argc; r++) {
        if (strcmp(argv[r], ">") == 0) {
            if (r + 1 >= *argc) {
                sh_error("sh: syntax error near >", NULL);
                close_redirects(*in_fd, *out_fd);
                return -1;
            }
            if (*out_fd >= 0) {
                close(*out_fd);
                *out_fd = -1;
            }
            int fd = open(argv[r + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                sh_errno_error("sh: cannot open ", argv[r + 1], errno);
                close_redirects(*in_fd, *out_fd);
                return -1;
            }
            int moved = move_fd_from_stdio(fd);
            if (moved < 0) {
                sh_error("sh: cannot move output fd", NULL);
                close(fd);
                close_redirects(*in_fd, *out_fd);
                return -1;
            }
            *out_fd = moved;
            r++;
        } else if (strcmp(argv[r], "<") == 0) {
            if (r + 1 >= *argc) {
                sh_error("sh: syntax error near <", NULL);
                close_redirects(*in_fd, *out_fd);
                return -1;
            }
            if (*in_fd >= 0) {
                close(*in_fd);
                *in_fd = -1;
            }
            int fd = open(argv[r + 1], O_RDONLY, 0);
            if (fd < 0) {
                sh_errno_error("sh: cannot open ", argv[r + 1], errno);
                close_redirects(*in_fd, *out_fd);
                return -1;
            }
            int moved = move_fd_from_stdio(fd);
            if (moved < 0) {
                sh_error("sh: cannot move input fd", NULL);
                close(fd);
                close_redirects(*in_fd, *out_fd);
                return -1;
            }
            *in_fd = moved;
            r++;
        } else {
            argv[w++] = argv[r];
        }
    }
    argv[w] = NULL;
    *argc = w;
    return 0;
}

/* Finds an unquoted "|" splitting argv into two command segments. Returns the
 * pipe index (the "|" position) or -1 when there is no pipe. */
static int find_pipe(int argc, char **argv) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "|") == 0)
            return i;
    return -1;
}

static int argv_count(char **argv) {
    int argc = 0;
    while (argv[argc] != NULL)
        argc++;
    return argc;
}

static int dup_non_stdio(int fd) {
    int temps[3] = {-1, -1, -1};
    for (int i = 0; i < 4; i++) {
        int moved = dup(fd);
        if (moved < 0) {
            break;
        }
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
        if (fds[i] < 2) {
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

static int move_fd_from_stdio(int fd) {
    if (fd >= 3)
        return fd;
    int moved = dup_non_stdio(fd);
    if (moved < 0)
        return -1;
    close(fd);
    return moved;
}

static int is_echo_builtin(char **argv) {
    return argv != NULL && argv[0] != NULL && strcmp(argv[0], "echo") == 0;
}

static int write_echo_to_fd(int fd, char **argv) {
    for (int i = 1; argv[i] != NULL; i++) {
        if (i > 1 && write(fd, " ", 1) != 1)
            return -1;
        size_t len = strlen(argv[i]);
        if (len != 0 && write(fd, argv[i], len) != (ssize_t)len)
            return -1;
    }
    return write(fd, "\n", 1) == 1 ? 0 : -1;
}

/* Runs a single-stage pipe: left | right. */
static int run_pipe(char **left, char **right) {
    int fds[2];
    if (pipe(fds) < 0) {
        sh_error("sh: pipe failed", NULL);
        return 126;
    }
    if (move_pipe_fds_from_stdio(fds) < 0) {
        close(fds[0]);
        close(fds[1]);
        sh_error("sh: pipe fd setup failed", NULL);
        restore_shell_foreground();
        return 126;
    }

    pid_t lpid = -1;
    if (!is_echo_builtin(left)) {
        lpid = fork();
        if (lpid < 0) {
            close(fds[0]);
            close(fds[1]);
            sh_error("sh: fork failed", NULL);
            restore_shell_foreground();
            return 126;
        }
        if (lpid == 0) {
            restore_child_signal_policy();
            (void)setpgid(0, 0);
            if (dup2(fds[1], 1) < 0)
                exit(126);
            close(fds[0]);
            close(fds[1]);
            int status = 0;
            if (run_builtin(argv_count(left), left, -1, 0, &status))
                exit(status);
            char direct[SH_PATH_MAX];
            int rc = build_direct_path(left[0], direct);
            if (rc == 1)
                execve(direct, left, environ);
            else if (rc == 0)
                rc = exec_with_path(left[0], left);
            if (rc < 0)
                sh_error("sh: path too long: ", left[0]);
            else
                sh_error("sh: command not found: ", left[0]);
            exit(rc < 0 ? 126 : 127);
        }
    }

    pid_t rpid = fork();
    if (rpid < 0) {
        close(fds[0]);
        close(fds[1]);
        if (lpid > 0)
            wait_status(lpid, NULL);
        sh_error("sh: fork failed", NULL);
        restore_shell_foreground();
        return 126;
    }
    if (rpid == 0) {
        restore_child_signal_policy();
        (void)setpgid(0, lpid > 0 ? lpid : 0);
        if (dup2(fds[0], 0) < 0)
            exit(126);
        close(fds[0]);
        close(fds[1]);
        int status = 0;
        if (run_builtin(argv_count(right), right, -1, 0, &status))
            exit(status);
        char direct[SH_PATH_MAX];
        int rc = build_direct_path(right[0], direct);
        if (rc == 1)
            execve(direct, right, environ);
        else if (rc == 0)
            rc = exec_with_path(right[0], right);
        if (rc < 0)
            sh_error("sh: path too long: ", right[0]);
        else
            sh_error("sh: command not found: ", right[0]);
        exit(rc < 0 ? 126 : 127);
    }

    const pid_t foreground_pgid = lpid > 0 ? lpid : rpid;
    if (lpid > 0 && setpgid(lpid, foreground_pgid) != 0)
        sh_errno_error("sh: pipe setpgid failed", left[0], errno);
    if (setpgid(rpid, foreground_pgid) != 0)
        sh_errno_error("sh: pipe setpgid failed", right[0], errno);
    if (tcsetpgrp(0, foreground_pgid) != 0)
        sh_errno_error("sh: pipe foreground setup failed", NULL, errno);

    if (is_echo_builtin(left)) {
        close(fds[0]);
        (void)write_echo_to_fd(fds[1], left);
        close(fds[1]);
        int right_status = 126;
        if (wait_status(rpid, &right_status) != rpid) {
            restore_shell_foreground();
            return 126;
        }
        restore_shell_foreground();
        return right_status;
    }

    close(fds[0]);
    close(fds[1]);
    int ignored_status = 0;
    int right_status = 126;
    wait_status(lpid, &ignored_status);
    if (wait_status(rpid, &right_status) != rpid) {
        restore_shell_foreground();
        return 126;
    }
    restore_shell_foreground();
    return right_status;
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    char line[SH_MAX_LINE];
    char *args[SH_MAX_ARGC + 1];
    int last_status = 0;
    install_shell_signal_policy();
    setup_shell_foreground();

    for (;;) {
        int interactive = is_interactive_session();
        const char *prompt = "$ ";
        if (interactive)
            write_all(1, prompt);
        int len = read_line(line, sizeof(line), interactive);
        if (len == SH_READ_LINE_EOF) {
            if (interactive)
                write_all(1, "\n");
            return last_status;
        }
        if (len == SH_READ_LINE_INTERRUPT) {
            last_status = 130;
            continue;
        }
        if (len == SH_READ_LINE_TOO_LONG) {
            sh_error("sh: line too long", NULL);
            last_status = 2;
            continue;
        }
        if (len == 0)
            continue;

        int n = tokenize(line, args);
        if (n < 0) {
            sh_error("sh: too many arguments", NULL);
            last_status = 2;
            continue;
        }
        if (n == 0)
            continue;
        if (contains_unsupported_token(n, args)) {
            last_status = 2;
            continue;
        }

        int pipe_idx = find_pipe(n, args);
        if (pipe_idx >= 0) {
            args[pipe_idx] = NULL;
            char **right = &args[pipe_idx + 1];
            if (args[0] == NULL || right[0] == NULL) {
                sh_error("sh: syntax error near |", NULL);
                last_status = 2;
                continue;
            }
            if (find_pipe(argv_count(right), right) >= 0) {
                sh_error("sh: unsupported syntax: multiple pipes", NULL);
                last_status = 2;
                continue;
            }
            if (contains_redirection_token(argv_count(args), args) ||
                contains_redirection_token(argv_count(right), right)) {
                sh_error("sh: unsupported syntax: redirection with pipe", NULL);
                last_status = 2;
                continue;
            }
            last_status = run_pipe(args, right);
            continue;
        }

        int in_fd, out_fd;
        if (apply_redirects(args, &n, &in_fd, &out_fd) != 0) {
            last_status = 2;
            continue;
        }
        if (n == 0) {
            close_redirects(in_fd, out_fd);
            last_status = 0;
            continue;
        }
        int status = 0;
        if (run_builtin(n, args, out_fd, last_status, &status)) {
            close_redirects(in_fd, out_fd);
            last_status = status;
            continue;
        }
        last_status = run_external(args, in_fd, out_fd);
        close_redirects(in_fd, out_fd);
    }
    return 0;
}
