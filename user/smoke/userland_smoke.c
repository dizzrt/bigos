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
 *     errno == ENOENT, failed execve leaves caller alive),
 *   - minimal malloc/free,
 *   - shell-style fork + execve + wait of external simple C programs, including
 *     bounded wait status observation,
 *   - single-stage pipe between two children, including writer-close EOF,
 *   - file redirection, cwd-relative paths, dot/dot-dot resolution, getcwd,
 *     fork cwd inheritance, exec cwd preservation, and dup/close fd sharing.
 *   - non-interactive /bin/sh execution of /bin/smoke probes, including
 *     stdout/stderr, errno reporting, pipe/redirection, unsupported syntax, and
 *     shell continuation after non-zero exit.
 *   - shell-launched bounded path tools, including PATH lookup, explicit paths,
 *     cwd-relative path handling, directory listing, metadata, mkdir, rm,
 *     redirection, a single pipe, and deterministic failure recovery.
 *   - the bounded libc subset probe for fine-grained headers, fprintf(stderr),
 *     string/memory semantics, read-only environment, and allocator failure.
 *   - bounded signal termination plus time and identity wrappers.
 */
#include "libc.h"

#define CAPTURE_MAX 2048

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

static int all_zero(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (buf[i] != 0)
            return 0;
    return 1;
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

static void run_program_expect(const char *path, char **child_argv, char **envp, int expected_status) {
    pid_t pid = fork();
    if (pid < 0)
        fail("program-fork");
    if (pid == 0) {
        execve(path, child_argv, envp);
        exit(127);
    }

    int status = -1;
    if (wait_status(pid, &status) != pid)
        fail("program-wait");
    if (status != expected_status)
        fail("program-status");
}

static void run_program(const char *path, char **child_argv, char **envp) {
    run_program_expect(path, child_argv, envp, 0);
}

static void require_file_contains(const char *path, const char *expect) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        if (errno == ENOENT) {
            emit("BIGOS_USERLAND_FAILED record-open-enoent path=");
            emit(path);
            emit("\n");
            exit(1);
        }
        if (errno == EWOULDBLOCK)
            fail("record-open-wouldblock");
        if (errno == EACCES)
            fail("record-open-acces");
        if (errno == EMFILE)
            fail("record-open-emfile");
        fail("record-open");
    }
    char buf[CAPTURE_MAX];
    size_t total = 0;
    for (;;) {
        size_t chunk = sizeof(buf) - 1 - total;
        if (chunk > 512)
            chunk = 512;
        ssize_t n = read(fd, buf + total, chunk);
        if (n < 0) {
            close(fd);
            fail("record-read");
        }
        if (n == 0)
            break;
        total += (size_t)n;
        if (total >= sizeof(buf) - 1)
            break;
    }
    close(fd);
    if (total == 0) {
        emit("BIGOS_USERLAND_FAILED record-empty path=");
        emit(path);
        emit("\n");
        exit(1);
    }
    buf[total] = 0;
    if (!contains(buf, expect)) {
        emit("BIGOS_USERLAND_FAILED record-content path=");
        emit(path);
        emit(" expect=");
        emit(expect);
        emit("\n");
        exit(1);
    }
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

/* failed execve reports errno and leaves the caller process able to continue. */
static void test_exec_failure(char **envp) {
    char *argv[] = {(char *)"/no/such/program", NULL};
    errno = 0;
    if (execve("/no/such/program", argv, envp) != -1 || errno != ENOENT)
        fail("exec-failure");
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
    pid_t reaped = waitpid(pid, NULL, 0);
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
    waitpid(pid, NULL, 0);
    if (n <= 0)
        fail("pipe-read");
    buf[n] = 0;
    if (strcmp(buf, "pipe-data") != 0)
        fail("pipe-content");
    if (read(fds[0], buf, sizeof(buf)) != 0)
        fail("pipe-eof");
    close(fds[0]);
}

/* file redirection and dup/close: duplicates share one bounded file object. */
static void test_redirect_and_dup(void) {
    const char *path = "/rw/smoke.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("redir-open");
    int dupfd = dup(fd);
    if (dupfd < 0)
        fail("dup");
    close(fd);
    const char *payload = "redir-ok";
    if (write(dupfd, payload, strlen(payload)) != (ssize_t)strlen(payload))
        fail("redir-write");
    close(dupfd);

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

static void test_fd_inheritance_and_offsets(void) {
    const char *path = "/rw/fd_inherit.txt";
    unlink(path);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("fd-inherit-open");
    if (write(fd, "abcdef", 6) != 6)
        fail("fd-inherit-write");
    if (lseek(fd, 0, SEEK_SET) != 0)
        fail("fd-inherit-seek");

    pid_t pid = fork();
    if (pid < 0)
        fail("fd-inherit-fork");
    if (pid == 0) {
        char child_buf[4];
        ssize_t n = read(fd, child_buf, 3);
        if (n != 3)
            exit(41);
        child_buf[3] = 0;
        exit(strcmp(child_buf, "abc") == 0 ? 0 : 42);
    }
    int status = -1;
    if (wait_status(pid, &status) != pid || status != 0)
        fail("fd-inherit-wait");
    char parent_buf[4];
    ssize_t n = read(fd, parent_buf, 3);
    if (n != 3)
        fail("fd-inherit-parent-read");
    parent_buf[3] = 0;
    if (strcmp(parent_buf, "def") != 0)
        fail("fd-inherit-parent-content");

    int fd2 = open(path, O_RDONLY, 0);
    if (fd2 < 0)
        fail("fd-independent-open");
    char ch1 = 0;
    char ch2 = 0;
    if (lseek(fd, 0, SEEK_SET) != 0)
        fail("fd-independent-seek");
    if (read(fd, &ch1, 1) != 1 || ch1 != 'a')
        fail("fd-independent-read1");
    if (read(fd2, &ch2, 1) != 1 || ch2 != 'a')
        fail("fd-independent-read2");
    close(fd2);
    close(fd);
}

static int dirents_contain(struct bigos_dirent *entries, ssize_t n, const char *name, unsigned int type) {
    for (ssize_t i = 0; i < n; i++) {
        if (entries[i].type == type && strcmp(entries[i].name, name) == 0)
            return 1;
    }
    return 0;
}

static void test_runtime_filesystem(void) {
    unlink("/rw/runtime_file.txt");
    unlink("/rw/runtime_unlink.txt");
    unlink("/rw/runtime_rename_src.txt");
    unlink("/rw/runtime_rename_dst.txt");
    unlink("/rw/runtime_rename_existing.txt");
    unlink("/rw/runtime_rename_ro.txt");
    unlink("/rw/runtime_full.txt");
    unlink("/rw/runtime_growth.txt");
    unlink("/rw/runtime_reuse_a.txt");
    unlink("/rw/runtime_reuse_b.txt");
    unlink("/rw/runtime_tree/sub/a.txt");
    unlink("/rw/runtime_tree/sub/b.txt");
    rmdir("/rw/runtime_tree/sub/empty");
    rmdir("/rw/runtime_tree/sub");
    rmdir("/rw/runtime_tree");
    if (mkdir("/rw/runtime_rename_dir", 0755) < 0 && errno != EEXIST)
        fail("runtime-rename-dir-setup");
    if (mkdir("/rw/runtime_dir", 0755) < 0 && errno != EEXIST)
        fail("runtime-mkdir");
    struct stat st;
    if (stat("/rw/runtime_dir", &st) != 0 || st.type != BIGOS_METADATA_TYPE_DIRECTORY || !S_ISDIR(st.st_mode))
        fail("runtime-stat-dir");
    if (stat("/boot/user/init.elf", &st) != 0 || st.type != BIGOS_METADATA_TYPE_REGULAR || st.st_size == 0 ||
        st.st_object_id == 0)
        fail("runtime-stat-exfat");

    int fd = open("/rw/runtime_file.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("runtime-open");
    const char *payload = "runtime-fs";
    if (write(fd, payload, strlen(payload)) != (ssize_t)strlen(payload))
        fail("runtime-write");
    if (fstat(fd, &st) != 0 || st.type != BIGOS_METADATA_TYPE_REGULAR || st.st_size != strlen(payload) ||
        st.st_uid != 0 || st.st_gid != 0 || st.st_object_id == 0)
        fail("runtime-fstat-file");
    if (lseek(fd, 0, SEEK_CUR) != (off_t)strlen(payload))
        fail("runtime-fstat-offset");
    if (fsync(fd) != 0)
        fail("runtime-fsync");
    if (sync() != 0)
        fail("runtime-sync");
    if (lseek(fd, 0, SEEK_SET) != 0)
        fail("runtime-seek");
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n != (ssize_t)strlen(payload))
        fail("runtime-read");
    buf[n] = 0;
    if (strcmp(buf, payload) != 0)
        fail("runtime-content");
    if (stat("/rw/runtime_file.txt", &st) != 0 || st.st_size != strlen(payload) || !S_ISREG(st.st_mode))
        fail("runtime-stat-file");

    fd = open("/rw/runtime_growth.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("runtime-growth-open");
    if (write(fd, "abc", 3) != 3)
        fail("runtime-growth-write");
    if (lseek(fd, 521, SEEK_SET) != 521)
        fail("runtime-growth-gap-seek");
    if (write(fd, "Z", 1) != 1)
        fail("runtime-growth-gap-write");
    if (fstat(fd, &st) != 0 || st.st_size != 522)
        fail("runtime-growth-size");
    if (lseek(fd, 3, SEEK_SET) != 3)
        fail("runtime-growth-gap-read-seek");
    char gap[8];
    n = read(fd, gap, sizeof(gap));
    if (n != (ssize_t)sizeof(gap) || !all_zero(gap, sizeof(gap)))
        fail("runtime-growth-gap-zero");
    if (lseek(fd, 510, SEEK_SET) != 510)
        fail("runtime-growth-cross-seek");
    if (write(fd, "uvwxy", 5) != 5)
        fail("runtime-growth-cross-write");
    if (lseek(fd, 510, SEEK_SET) != 510)
        fail("runtime-growth-cross-read-seek");
    char cross[6];
    n = read(fd, cross, 5);
    if (n != 5)
        fail("runtime-growth-cross-read");
    cross[5] = 0;
    if (strcmp(cross, "uvwxy") != 0)
        fail("runtime-growth-cross-content");
    if (ftruncate(fd, 2) != 0)
        fail("runtime-ftruncate-shrink");
    if (fstat(fd, &st) != 0 || st.st_size != 2)
        fail("runtime-ftruncate-shrink-size");
    if (lseek(fd, 2, SEEK_SET) != 2 || read(fd, gap, sizeof(gap)) != 0)
        fail("runtime-ftruncate-shrink-eof");
    if (ftruncate(fd, 10) != 0)
        fail("runtime-ftruncate-extend");
    if (fstat(fd, &st) != 0 || st.st_size != 10)
        fail("runtime-ftruncate-extend-size");
    if (lseek(fd, 2, SEEK_SET) != 2)
        fail("runtime-ftruncate-extend-seek");
    n = read(fd, gap, sizeof(gap));
    if (n != (ssize_t)sizeof(gap) || !all_zero(gap, sizeof(gap)))
        fail("runtime-ftruncate-extend-zero");
    errno = 0;
    if (ftruncate(fd, 4097) != -1 || errno != ENOSPC)
        fail("runtime-ftruncate-enospc");
    if (fstat(fd, &st) != 0 || st.st_size != 10)
        fail("runtime-ftruncate-enospc-size");
    close(fd);
    if (truncate("/rw/runtime_growth.txt", 1) != 0)
        fail("runtime-truncate-path");
    if (stat("/rw/runtime_growth.txt", &st) != 0 || st.st_size != 1)
        fail("runtime-truncate-path-size");
    errno = 0;
    if (truncate("/boot/user/init.elf", 0) != -1 || errno != EROFS)
        fail("runtime-truncate-rofs");
    int reuse = open("/rw/runtime_reuse_a.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (reuse < 0)
        fail("runtime-reuse-open-a");
    char fill[512];
    for (size_t i = 0; i < sizeof(fill); i++)
        fill[i] = 'Q';
    if (write(reuse, fill, sizeof(fill)) != (ssize_t)sizeof(fill))
        fail("runtime-reuse-write-a");
    if (ftruncate(reuse, 0) != 0)
        fail("runtime-reuse-truncate-a");
    close(reuse);
    reuse = open("/rw/runtime_reuse_b.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (reuse < 0)
        fail("runtime-reuse-open-b");
    if (ftruncate(reuse, 16) != 0)
        fail("runtime-reuse-truncate-b");
    char zeros[16];
    if (lseek(reuse, 0, SEEK_SET) != 0 || read(reuse, zeros, sizeof(zeros)) != (ssize_t)sizeof(zeros) ||
        !all_zero(zeros, sizeof(zeros)))
        fail("runtime-reuse-zero");
    close(reuse);

    struct bigos_dirent entries[BIGOS_DIRENT_MAX_BATCH];
    fd = open("/rw/runtime_file.txt", O_RDONLY, 0);
    if (fd < 0)
        fail("runtime-reopen-file");
    errno = 0;
    if (bigos_readdir(fd, entries, 1) != -1 || errno != ENOTDIR)
        fail("runtime-readdir-file-enotdir");
    errno = 0;
    if (read(fd, (void *)1, 1) != -1 || errno != EFAULT)
        fail("runtime-read-efault");
    close(fd);
    errno = 0;
    if (stat("/rw/runtime_file.txt", (struct stat *)1) != -1 || errno != EFAULT)
        fail("runtime-stat-efault");

    int dirfd = open("/rw", O_RDONLY, 0);
    if (dirfd < 0)
        fail("runtime-dir-open");
    if (fstat(dirfd, &st) != 0 || st.type != BIGOS_METADATA_TYPE_DIRECTORY)
        fail("runtime-fstat-dir");
    errno = 0;
    if (bigos_readdir(dirfd, entries, BIGOS_DIRENT_MAX_BATCH + 1) != -1 || errno != ERANGE)
        fail("runtime-readdir-erange");
    ssize_t count = bigos_readdir(dirfd, entries, BIGOS_DIRENT_MAX_BATCH);
    close(dirfd);
    if (count < 0)
        fail("runtime-readdir");
    if (!dirents_contain(entries, count, "runtime_file.txt", BIGOS_DIRENT_TYPE_FILE))
        fail("runtime-readdir-file");
    if (!dirents_contain(entries, count, "runtime_dir", BIGOS_DIRENT_TYPE_DIRECTORY))
        fail("runtime-readdir-dir");

    if (mkdir("/rw/runtime_tree", 0755) != 0)
        fail("runtime-tree-mkdir");
    if (mkdir("/rw/runtime_tree/sub", 0755) != 0)
        fail("runtime-tree-mkdir-sub");
    if (mkdir("/rw/runtime_tree/sub/empty", 0755) != 0)
        fail("runtime-tree-mkdir-empty");
    int tree_a = open("/rw/runtime_tree/sub/a.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int tree_b = open("/rw/runtime_tree/sub/b.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (tree_a < 0 || tree_b < 0)
        fail("runtime-tree-open");
    if (write(tree_a, "tree-a", 6) != 6 || write(tree_b, "tree-b", 6) != 6)
        fail("runtime-tree-write");
    close(tree_a);
    close(tree_b);
    if (stat("/rw/runtime_tree/sub/a.txt", &st) != 0 || st.st_size != 6 || !S_ISREG(st.st_mode))
        fail("runtime-tree-stat-file");
    int tree_dir = open("/rw/runtime_tree/sub", O_RDONLY, 0);
    if (tree_dir < 0)
        fail("runtime-tree-open-dir");
    count = bigos_readdir(tree_dir, entries, BIGOS_DIRENT_MAX_BATCH);
    close(tree_dir);
    if (count < 0 || !dirents_contain(entries, count, "a.txt", BIGOS_DIRENT_TYPE_FILE) ||
        !dirents_contain(entries, count, "b.txt", BIGOS_DIRENT_TYPE_FILE) ||
        !dirents_contain(entries, count, "empty", BIGOS_DIRENT_TYPE_DIRECTORY))
        fail("runtime-tree-readdir");
    errno = 0;
    if (rmdir("/rw/runtime_tree/sub") != -1 || errno != ENOTEMPTY)
        fail("runtime-tree-rmdir-not-empty");
    errno = 0;
    if (rmdir("/rw/runtime_tree/sub/a.txt") != -1 || errno != ENOTDIR)
        fail("runtime-tree-rmdir-file");
    errno = 0;
    if (rmdir("/boot") != -1 || errno != EROFS)
        fail("runtime-tree-rmdir-rofs");
    errno = 0;
    if (unlink("/rw/runtime_tree/sub/empty") != -1 || errno != EISDIR)
        fail("runtime-tree-unlink-dir");
    if (rmdir("/rw/runtime_tree/sub/empty") != 0)
        fail("runtime-tree-rmdir-empty");
    if (unlink("/rw/runtime_tree/sub/a.txt") != 0)
        fail("runtime-tree-unlink-a");
    tree_dir = open("/rw/runtime_tree/sub", O_RDONLY, 0);
    if (tree_dir < 0)
        fail("runtime-tree-open-dir2");
    count = bigos_readdir(tree_dir, entries, BIGOS_DIRENT_MAX_BATCH);
    close(tree_dir);
    if (count < 0 || dirents_contain(entries, count, "a.txt", BIGOS_DIRENT_TYPE_FILE) ||
        !dirents_contain(entries, count, "b.txt", BIGOS_DIRENT_TYPE_FILE))
        fail("runtime-tree-readdir-after-unlink");
    if (unlink("/rw/runtime_tree/sub/b.txt") != 0)
        fail("runtime-tree-unlink-b");
    if (rmdir("/rw/runtime_tree/sub") != 0)
        fail("runtime-tree-rmdir-sub");
    if (rmdir("/rw/runtime_tree") != 0)
        fail("runtime-tree-rmdir-root");

    fd = open("/rw/runtime_rename_src.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("runtime-rename-open");
    if (write(fd, "rename-data", 11) != 11)
        fail("runtime-rename-write");
    if (rename("/rw/runtime_rename_src.txt", "/rw/runtime_rename_dst.txt") != 0)
        fail("runtime-rename");
    if (stat("/rw/runtime_rename_src.txt", &st) != -1 || errno != ENOENT)
        fail("runtime-rename-source-gone");
    if (stat("/rw/runtime_rename_dst.txt", &st) != 0 || st.st_size != 11 || !S_ISREG(st.st_mode))
        fail("runtime-rename-stat");
    if (lseek(fd, 0, SEEK_SET) != 0)
        fail("runtime-rename-open-seek");
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n != 11)
        fail("runtime-rename-open-read");
    buf[n] = 0;
    if (strcmp(buf, "rename-data") != 0)
        fail("runtime-rename-open-content");
    require_file_contains("/rw/runtime_rename_dst.txt", "rename-data");
    if (rename("/rw/runtime_rename_dst.txt", "/rw/runtime_rename_dst.txt") != 0)
        fail("runtime-rename-noop");

    int existing = open("/rw/runtime_rename_existing.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (existing < 0)
        fail("runtime-rename-existing-open");
    if (write(existing, "existing", 8) != 8)
        fail("runtime-rename-existing-write");
    close(existing);
    errno = 0;
    if (rename("/rw/runtime_rename_dst.txt", "/rw/runtime_rename_existing.txt") != -1 || errno != EEXIST)
        fail("runtime-rename-eexist");
    require_file_contains("/rw/runtime_rename_dst.txt", "rename-data");
    require_file_contains("/rw/runtime_rename_existing.txt", "existing");
    errno = 0;
    if (rename("/rw/runtime_rename_missing.txt", "/rw/runtime_rename_new.txt") != -1 || errno != ENOENT)
        fail("runtime-rename-missing");
    errno = 0;
    if (rename("/boot/user/init.elf", "/rw/runtime_rename_ro.txt") != -1 || errno != EROFS)
        fail("runtime-rename-rofs-old");
    errno = 0;
    if (rename("/rw/runtime_rename_dst.txt", "/boot/runtime_rename_dst.txt") != -1 || errno != EROFS)
        fail("runtime-rename-rofs-new");
    errno = 0;
    if (rename("/rw/runtime_rename_dir", "/rw/runtime_rename_dir_new") != -1 || errno != EISDIR)
        fail("runtime-rename-dir");

    fd = open("/rw/runtime_unlink.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("runtime-unlink-open");
    if (write(fd, "gone", 4) != 4)
        fail("runtime-unlink-write");
    if (unlink("/rw/runtime_unlink.txt") != 0)
        fail("runtime-unlink");
    if (open("/rw/runtime_unlink.txt", O_RDONLY, 0) != -1 || errno != ENOENT)
        fail("runtime-unlink-lookup");
    if (stat("/rw/runtime_unlink.txt", &st) != -1 || errno != ENOENT)
        fail("runtime-unlink-stat");
    if (fstat(fd, &st) != 0 || st.st_size != 4)
        fail("runtime-unlink-fstat");
    if (lseek(fd, 0, SEEK_SET) != 0)
        fail("runtime-unlink-seek");
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n != 4)
        fail("runtime-unlink-read");
    buf[n] = 0;
    if (strcmp(buf, "gone") != 0)
        fail("runtime-unlink-content");

    errno = 0;
    fd = open("/boot/user/init.elf", O_WRONLY, 0);
    if (fd != -1 || errno != EROFS)
        fail("runtime-rofs");
    errno = 0;
    if (fstat(250, &st) != -1 || errno != EBADF)
        fail("runtime-fstat-badfd");

    fd = open("/rw/runtime_full.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("runtime-full-open");
    if (lseek(fd, 4095, SEEK_SET) != 4095)
        fail("runtime-full-seek");
    if (write(fd, "z", 1) != 1)
        fail("runtime-full-write");
    errno = 0;
    if (write(fd, "x", 1) != -1 || errno != ENOSPC)
        fail("runtime-full-enospc");
    if (fstat(fd, &st) != 0 || st.st_size != 4096)
        fail("runtime-full-size");
    if (lseek(fd, 0, SEEK_CUR) != 4096)
        fail("runtime-full-offset");
    close(fd);
}

static void test_current_directory(char **envp) {
    unlink("/rw/cwd_test/note.txt");
    unlink("/rw/cwd_test/child.txt");
    unlink("/rw/cwd_test/pwd.txt");
    unlink("/rw/cwd_test/sub/rel_old.txt");
    unlink("/rw/cwd_test/sub/rel_new.txt");
    if (mkdir("/rw/cwd_test", 0755) < 0 && errno != EEXIST)
        fail("cwd-mkdir");
    if (mkdir("/rw/cwd_test/sub", 0755) < 0 && errno != EEXIST)
        fail("cwd-mkdir-sub");

    int fd = open("/rw/cwd_test/note.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fail("cwd-note-open");
    if (write(fd, "cwd-note", 8) != 8)
        fail("cwd-note-write");
    close(fd);

    if (chdir("/rw/cwd_test/sub") != 0)
        fail("cwd-chdir-sub");
    char cwd[256 + 1];
    if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/rw/cwd_test/sub") != 0)
        fail("cwd-getcwd");
    char small[2];
    errno = 0;
    if (getcwd(small, sizeof(small)) != NULL || errno != ERANGE)
        fail("cwd-erange");

    int rel_fd = open("rel_old.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (rel_fd < 0)
        fail("cwd-rename-open");
    if (write(rel_fd, "relative-rename", 15) != 15)
        fail("cwd-rename-write");
    close(rel_fd);
    if (rename("rel_old.txt", "rel_new.txt") != 0)
        fail("cwd-rename");
    require_file_contains("/rw/cwd_test/sub/rel_new.txt", "relative-rename");

    fd = open("../note.txt", O_RDONLY, 0);
    if (fd < 0)
        fail("cwd-open-dotdot");
    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n != 8)
        fail("cwd-read-dotdot");
    buf[n] = 0;
    if (strcmp(buf, "cwd-note") != 0)
        fail("cwd-content-dotdot");
    struct stat st;
    if (stat("../note.txt", &st) != 0 || st.st_size != 8)
        fail("cwd-stat-dotdot");

    pid_t pid = fork();
    if (pid < 0)
        fail("cwd-fork");
    if (pid == 0) {
        int child_fd = open("../child.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (child_fd < 0)
            exit(11);
        if (write(child_fd, "child-cwd", 9) != 9)
            exit(12);
        close(child_fd);
        exit(0);
    }
    if (chdir("/") != 0)
        fail("cwd-parent-restore");
    int status = -1;
    if (wait_status(pid, &status) != pid || status != 0)
        fail("cwd-fork-wait");
    require_file_contains("/rw/cwd_test/child.txt", "child-cwd");

    if (chdir("/rw/cwd_test") != 0)
        fail("cwd-chdir-parent");
    rmdir("/rw/cwd_test/deleted");
    if (mkdir("/rw/cwd_test/deleted", 0755) != 0)
        fail("cwd-deleted-mkdir");
    if (chdir("/rw/cwd_test/deleted") != 0)
        fail("cwd-deleted-chdir");
    if (rmdir("/rw/cwd_test/deleted") != 0)
        fail("cwd-deleted-rmdir");
    if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/rw/cwd_test/deleted") != 0)
        fail("cwd-deleted-getcwd");
    errno = 0;
    if (open("new.txt", O_RDWR | O_CREAT | O_TRUNC, 0644) != -1 || errno != ENOENT)
        fail("cwd-deleted-relative");
    if (chdir("..") != 0)
        fail("cwd-deleted-dotdot");
    if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/rw/cwd_test") != 0)
        fail("cwd-deleted-dotdot-cwd");
    char *pwd_argv[] = {(char *)"/bin/pwd", NULL};
    run_program("/bin/pwd", pwd_argv, envp);
    if (chdir("/") != 0)
        fail("cwd-final-restore");
}

static void test_time_identity(void) {
    if (getpid() <= 0)
        fail("getpid");
    if (getppid() < 0)
        fail("getppid");
    if (getuid() != 0 || getgid() != 0)
        fail("identity");
    long now = time_now();
    if (now <= 0)
        fail("time");
    time_t out = 0;
    time_t now2 = time(&out);
    if (now2 <= 0 || out != now2)
        fail("time-wrapper");
    unsigned long tick0 = get_tick();
    unsigned long tick1 = get_tick();
    if (tick1 < tick0)
        fail("tick");
}

static void test_wait_wrappers(char **envp) {
    (void)envp;
    pid_t pid = fork();
    if (pid < 0)
        fail("waitpid-fork");
    if (pid == 0) {
        exit(5);
    }
    int status = -1;
    errno = 0;
    if (waitpid(pid, &status, 1) != -1 || errno != EINVAL)
        fail("waitpid-options");
    if (wait(&status) != pid || status != 5)
        fail("wait-any-status");

    pid = fork();
    if (pid < 0)
        fail("wait-specific-fork");
    if (pid == 0)
        exit(6);
    status = -1;
    if (waitpid(pid, &status, 0) != pid || status != 6)
        fail("waitpid-status");
}

static void test_error_text(void) {
    if (strcmp(strerror(ENOENT), "No such file or directory") != 0)
        fail("strerror-known");
    if (strcmp(strerror(12345), "Unknown error") != 0)
        fail("strerror-unknown");
    errno = ENOENT;
    perror("smoke_perror");
}

static volatile int g_signal_handler_seen;
static volatile unsigned long g_signal_spin;

static void smoke_usr1_handler(int signo) {
    if (signo == SIGUSR1)
        g_signal_handler_seen = 1;
}

static void spin_in_user_mode(unsigned long rounds) {
    for (unsigned long i = 0; i < rounds; i++)
        g_signal_spin += i | 1ul;
}

static void test_signal_handler_return(void) {
    struct sigaction act;
    struct sigaction oldact;
    act.sa_handler = smoke_usr1_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGUSR1, &act, &oldact) != 0)
        fail("sigaction-install");
    if (oldact.sa_handler != SIG_DFL)
        fail("sigaction-old");

    sigset_t set;
    sigset_t oldmask;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &set, &oldmask) != 0)
        fail("sigmask-block");
    if (oldmask != 0)
        fail("sigmask-old");
    if (kill(getpid(), SIGUSR1) != 0)
        fail("sigmask-kill");
    spin_in_user_mode(200000ul);
    if (g_signal_handler_seen != 0)
        fail("sigmask-blocked");
    if (sigprocmask(SIG_UNBLOCK, &set, &oldmask) != 0)
        fail("sigmask-unblock");
    if ((oldmask & (1ul << (SIGUSR1 - 1))) == 0)
        fail("sigmask-unblock-old");
    for (int i = 0; i < 80 && g_signal_handler_seen == 0; i++)
        spin_in_user_mode(200000ul);
    if (g_signal_handler_seen != 1)
        fail("signal-handler-return");
}

static void test_signal_default_terminate(void) {
    pid_t pid = fork();
    if (pid < 0)
        fail("signal-fork");
    if (pid == 0) {
        for (;;)
            spin_in_user_mode(200000ul);
    }

    if (kill(pid, 9) != 0)
        fail("signal-kill");
    int status = 0;
    if (wait_status(pid, &status) != pid)
        fail("signal-wait");
    if (status != -(128 + 9))
        fail("signal-status");
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
    run_program_expect("/bin/smoke/exit", exit_argv, envp, 7);
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
    unlink("/rw/smoke_shell_io.txt");
    unlink("/rw/smoke_shell_ls_rw.txt");
    unlink("/rw/smoke_shell_stat.txt");
    unlink("/rw/smoke_shell_path_dir/nested/file.txt");
    rmdir("/rw/smoke_shell_path_dir/nested");
    rmdir("/rw/smoke_shell_path_dir");
    int transcript = open("/rw/smoke_shell_io.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (transcript < 0)
        fail("shell-transcript-open");
    if (transcript < 3) {
        int moved = dup_non_stdio(transcript);
        if (moved < 0)
            fail("shell-transcript-fd");
        close(transcript);
        transcript = moved;
    }

    pid_t pid = fork();
    if (pid < 0)
        fail("shell-fork");
    if (pid == 0) {
        close(input[1]);
        dup2(input[0], 0);
        dup2(transcript, 1);
        dup2(transcript, 2);
        close(input[0]);
        close(transcript);
        char *argv[] = {(char *)"/bin/sh", NULL};
        execve("/bin/sh", argv, envp);
        exit(127);
    }

    close(input[0]);
    close(transcript);
    write_all_or_exit(input[1], "cd /rw\n");
    write_all_or_exit(input[1], "/bin/pwd\n");
    write_all_or_exit(input[1], "mkdir smoke_shell_path_dir\n");
    write_all_or_exit(input[1], "mkdir smoke_shell_path_dir/nested\n");
    write_all_or_exit(input[1], "echo nested-ok > smoke_shell_path_dir/nested/file.txt\n");
    write_all_or_exit(input[1], "ls smoke_shell_path_dir > smoke_shell_ls_rw.txt\n");
    write_all_or_exit(input[1], "/bin/stat smoke_shell_path_dir/nested/file.txt > smoke_shell_stat.txt\n");
    write_all_or_exit(input[1], "/bin/rmdir smoke_shell_path_dir/nested\n");
    write_all_or_exit(input[1], "/bin/rm smoke_shell_path_dir/nested/file.txt\n");
    write_all_or_exit(input[1], "/bin/rmdir smoke_shell_path_dir/nested\n");
    write_all_or_exit(input[1], "/bin/stat smoke_shell_path_dir/nested\n");
    write_all_or_exit(input[1], "status\n");
    write_all_or_exit(input[1], "sync\n");
    write_all_or_exit(input[1], "echo shell-alive\n");
    write_all_or_exit(input[1], "exit 0\n");
    close(input[1]);
    if (waitpid(pid, NULL, 0) != pid)
        fail("shell-wait");

    require_file_contains("/rw/smoke_shell_io.txt", "/rw");
    require_file_contains("/rw/smoke_shell_ls_rw.txt", "dir nested");
    require_file_contains("/rw/smoke_shell_stat.txt", "path=smoke_shell_path_dir/nested/file.txt type=file");
    require_file_contains("/rw/smoke_shell_io.txt", "rmdir: smoke_shell_path_dir/nested: errno=39");
    require_file_contains("/rw/smoke_shell_io.txt", "stat: smoke_shell_path_dir/nested: errno=2");
    require_file_contains("/rw/smoke_shell_io.txt", "status 1");
    require_file_contains("/rw/smoke_shell_io.txt", "shell-alive");
}

int main(int argc, char **argv, char **envp) {
    test_crt0(argc, argv);
    test_errno();
    test_exec_failure(envp);
    test_malloc();
    test_fork_exec(envp);
    test_pipe();
    test_redirect_and_dup();
    test_fd_inheritance_and_offsets();
    test_runtime_filesystem();
    test_current_directory(envp);
    test_time_identity();
    test_wait_wrappers(envp);
    test_error_text();
    test_signal_handler_return();
    test_smoke_programs(envp);
    test_smoke_shell(envp);
    test_signal_default_terminate();
    emit("BIGOS_FILESYSTEM_MATURITY_PASSED\n");
    emit("BIGOS_USERLAND_PASSED\n");
    /* Idle: as PID-1 this must not exit; reap any further children. */
    for (;;) {
        if (wait(NULL) < 0) {
        }
    }
    return 0;
}
