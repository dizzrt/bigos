/* BigOS /bin/sh: minimal interactive shell.
 *
 * Read-parse-execute loop linked against crt0 and the user libc. Supports:
 *   - whitespace tokenization into a bounded argv,
 *   - builtins: exit, echo, cd,
 *   - external commands via PATH lookup + fork + execve + wait,
 *   - a single-stage pipe a | b,
 *   - basic > / < redirection.
 * All capacities are bounded and over-limit input is a deterministic error that
 * returns to the read loop instead of crashing. No job control, variable
 * expansion, globbing, or scripting control flow. */
#include "libc.h"

#define SH_MAX_LINE     256
#define SH_MAX_ARGC     32
#define SH_PATH_MAX     256
#define SH_DEFAULT_PATH "/bin"

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

static int should_echo_input(char ch) {
    return ch == '\t' || (ch >= ' ' && ch < 0x7f);
}

/* Reads one newline-terminated line from stdin into buf (bounded by cap-1).
 * Returns the length, 0 on EOF with no data, or -1 on an over-length line
 * (the remainder of the line is drained). */
static int read_line(char *buf, int cap, int interactive) {
    int len = 0;
    for (;;) {
        char ch;
        ssize_t n = read(0, &ch, 1);
        if (n <= 0)
            return len > 0 ? len : 0;
        if (ch == '\n') {
            if (interactive)
                write_all(1, "\n");
            buf[len] = 0;
            return len;
        }
        if (ch == '\b' || ch == 0x7f) {
            if (len > 0) {
                len--;
                if (interactive)
                    write_all(1, "\b");
            }
            continue;
        }
        if (len >= cap - 1) {
            /* Drain the rest of the over-length line. */
            while (n > 0 && ch != '\n')
                n = read(0, &ch, 1);
            return -1;
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
 * image on success). */
static void exec_with_path(const char *cmd, char **argv) {
    const char *path = getenv("PATH");
    if (path == NULL || *path == 0)
        path = SH_DEFAULT_PATH;

    char candidate[SH_PATH_MAX];
    const char *dir = path;
    while (dir != NULL) {
        const char *end = strchr(dir, ':');
        size_t dir_len = end != NULL ? (size_t)(end - dir) : strlen(dir);
        size_t cmd_len = strlen(cmd);
        if (dir_len + 1 + cmd_len < SH_PATH_MAX) {
            size_t i = 0;
            for (; i < dir_len; i++)
                candidate[i] = dir[i];
            if (dir_len == 0 || candidate[dir_len - 1] != '/')
                candidate[i++] = '/';
            strcpy(candidate + i, cmd);
            execve(candidate, argv, environ);
            if (errno != ENOENT)
                break; /* a real error, not "try next dir" */
        }
        dir = end != NULL ? end + 1 : NULL;
    }
}

/* Runs an external command (already tokenized argv). Forks; the child execve's
 * the resolved path; the parent waits. On execve failure the child reports and
 * exits non-zero, leaving the parent shell intact. */
static void run_external(char **argv, int in_fd, int out_fd) {
    pid_t pid = fork();
    if (pid < 0) {
        sh_error("sh: fork failed", NULL);
        return;
    }
    if (pid == 0) {
        if (in_fd >= 0) {
            dup2(in_fd, 0);
            close(in_fd);
        }
        if (out_fd >= 0) {
            dup2(out_fd, 1);
            close(out_fd);
        }
        char direct[SH_PATH_MAX];
        int rc = build_direct_path(argv[0], direct);
        if (rc == 1)
            execve(direct, argv, environ);
        else if (rc == 0)
            exec_with_path(argv[0], argv);
        sh_error("sh: command not found: ", argv[0]);
        exit(127);
    }
    wait(pid);
}

/* Handles builtins. Returns 1 if handled, 0 otherwise. */
static int run_builtin(int argc, char **argv, int out_fd) {
    if (strcmp(argv[0], "exit") == 0) {
        int code = 0;
        if (argc > 1) {
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
    if (strcmp(argv[0], "echo") == 0) {
        int fd = out_fd >= 0 ? out_fd : 1;
        for (int i = 1; i < argc; i++) {
            if (i > 1)
                write_all(fd, " ");
            size_t len = strlen(argv[i]);
            if (len != 0)
                write(fd, argv[i], len);
        }
        write_all(fd, "\n");
        return 1;
    }
    if (strcmp(argv[0], "cd") == 0) {
        if (out_fd >= 0) {
            sh_error("sh: cd does not write output", NULL);
            return 1;
        }
        if (argc != 2) {
            sh_error("sh: cd: usage: cd PATH", NULL);
            return 1;
        }
        if (chdir(argv[1]) != 0)
            sh_error("sh: cd failed: ", argv[1]);
        return 1;
    }
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
                return -1;
            }
            int fd = open(argv[r + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                sh_error("sh: cannot open ", argv[r + 1]);
                return -1;
            }
            fd = move_fd_from_stdio(fd);
            if (fd < 0) {
                sh_error("sh: cannot move output fd", NULL);
                return -1;
            }
            *out_fd = fd;
            r++;
        } else if (strcmp(argv[r], "<") == 0) {
            if (r + 1 >= *argc) {
                sh_error("sh: syntax error near <", NULL);
                return -1;
            }
            int fd = open(argv[r + 1], O_RDONLY, 0);
            if (fd < 0) {
                sh_error("sh: cannot open ", argv[r + 1]);
                return -1;
            }
            fd = move_fd_from_stdio(fd);
            if (fd < 0) {
                sh_error("sh: cannot move input fd", NULL);
                return -1;
            }
            *in_fd = fd;
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
static void run_pipe(char **left, char **right) {
    int fds[2];
    if (pipe(fds) < 0) {
        sh_error("sh: pipe failed", NULL);
        return;
    }
    if (move_pipe_fds_from_stdio(fds) < 0) {
        close(fds[0]);
        close(fds[1]);
        sh_error("sh: pipe fd setup failed", NULL);
        return;
    }

    pid_t lpid = -1;
    if (!is_echo_builtin(left)) {
        lpid = fork();
        if (lpid < 0) {
            close(fds[0]);
            close(fds[1]);
            sh_error("sh: fork failed", NULL);
            return;
        }
        if (lpid == 0) {
            if (dup2(fds[1], 1) < 0)
                exit(126);
            close(fds[0]);
            close(fds[1]);
            if (run_builtin(argv_count(left), left, -1))
                exit(0);
            char direct[SH_PATH_MAX];
            int rc = build_direct_path(left[0], direct);
            if (rc == 1)
                execve(direct, left, environ);
            else if (rc == 0)
                exec_with_path(left[0], left);
            sh_error("sh: command not found: ", left[0]);
            exit(127);
        }
    }

    pid_t rpid = fork();
    if (rpid < 0) {
        close(fds[0]);
        close(fds[1]);
        if (lpid > 0)
            wait(lpid);
        sh_error("sh: fork failed", NULL);
        return;
    }
    if (rpid == 0) {
        if (dup2(fds[0], 0) < 0)
            exit(126);
        close(fds[0]);
        close(fds[1]);
        if (run_builtin(argv_count(right), right, -1))
            exit(0);
        char direct[SH_PATH_MAX];
        int rc = build_direct_path(right[0], direct);
        if (rc == 1)
            execve(direct, right, environ);
        else if (rc == 0)
            exec_with_path(right[0], right);
        sh_error("sh: command not found: ", right[0]);
        exit(127);
    }

    if (is_echo_builtin(left)) {
        close(fds[0]);
        (void)write_echo_to_fd(fds[1], left);
        close(fds[1]);
        wait(rpid);
        return;
    }

    close(fds[0]);
    close(fds[1]);
    wait(lpid);
    wait(rpid);
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    char line[SH_MAX_LINE];
    char *args[SH_MAX_ARGC + 1];

    for (;;) {
        int interactive = is_interactive_session();
        const char *prompt = "$ ";
        if (interactive)
            write_all(1, prompt);
        int len = read_line(line, sizeof(line), interactive);
        if (len < 0) {
            sh_error("sh: line too long", NULL);
            continue;
        }
        if (len == 0)
            continue;

        int n = tokenize(line, args);
        if (n < 0) {
            sh_error("sh: too many arguments", NULL);
            continue;
        }
        if (n == 0)
            continue;

        int pipe_idx = find_pipe(n, args);
        if (pipe_idx >= 0) {
            args[pipe_idx] = NULL;
            char **right = &args[pipe_idx + 1];
            if (args[0] == NULL || right[0] == NULL) {
                sh_error("sh: syntax error near |", NULL);
                continue;
            }
            run_pipe(args, right);
            continue;
        }

        int in_fd, out_fd;
        if (apply_redirects(args, &n, &in_fd, &out_fd) != 0)
            continue;
        if (n == 0) {
            if (in_fd >= 0)
                close(in_fd);
            if (out_fd >= 0)
                close(out_fd);
            continue;
        }
        if (run_builtin(n, args, out_fd)) {
            if (in_fd >= 0)
                close(in_fd);
            if (out_fd >= 0)
                close(out_fd);
            continue;
        }
        run_external(args, in_fd, out_fd);
        if (in_fd >= 0)
            close(in_fd);
        if (out_fd >= 0)
            close(out_fd);
    }
    return 0;
}
