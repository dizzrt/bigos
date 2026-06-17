/* BigOS user libc: raw syscall primitives and wrappers.
 *
 * The kernel int 0x80 ABI: number in rax; args in rdi/rsi/rdx/r10/r8/r9; return
 * value in rax. The 4th arg uses r10 (not rcx). Wrappers translate a negative
 * kernel return into errno = -ret and a -1/sentinel result (POSIX convention).
 */
#include "libc.h"
#include "bigos_syscall.h"
#include "sys_nr.h"

int errno = 0;

#define BIGOS_SIG_MIN 1
#define BIGOS_SIG_MAX 31
#define BIGOS_SIG_COUNT BIGOS_SIG_MAX
#define BIGOS_SIGACTION_DEFAULT 0
#define BIGOS_SIGACTION_IGNORE  1
#define BIGOS_SIGACTION_HANDLER 2

struct bigos_signal_disp {
    unsigned long action;
    unsigned long handler;
};

static sighandler_t g_signal_handlers[BIGOS_SIG_COUNT + 1];
char __bigos_signal_stack[1024] __attribute__((aligned(16)));

void __bigos_signal_dispatch(int signo);
void __bigos_signal_trampoline(void);

__asm__(".global __bigos_signal_trampoline\n"
        "__bigos_signal_trampoline:\n"
        "    mov %rsp, %r12\n"
        "    lea __bigos_signal_stack+1024(%rip), %rsp\n"
        "    and $-16, %rsp\n"
        "    call __bigos_signal_dispatch\n"
        "    mov %r12, %rsp\n"
        "    mov $19, %rax\n"
        "    int $0x80\n"
        "1:\n"
        "    jmp 1b\n");

long syscall0(long n) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

long syscall1(long n, long a0) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a0) : "rcx", "r11", "memory");
    return ret;
}

long syscall2(long n, long a0, long a1) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1) : "rcx", "r11", "memory");
    return ret;
}

long syscall3(long n, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return ret;
}

long syscall4(long n, long a0, long a1, long a2, long a3) {
    long ret;
    register long r10 __asm__("r10") = a3;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

long syscall5(long n, long a0, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a3;
    register long r8 __asm__("r8") = a4;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a3;
    register long r8 __asm__("r8") = a4;
    register long r9 __asm__("r9") = a5;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

/* Translate a negative kernel return into errno + a -1 failure result. */
static long errno_translate(long ret) {
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return ret;
}

ssize_t write(int fd, const void *buf, size_t len) {
    return (ssize_t)errno_translate(syscall3(SYS_WRITE, (long)fd, (long)buf, (long)len));
}

ssize_t read(int fd, void *buf, size_t len) {
    return (ssize_t)errno_translate(syscall3(SYS_READ, (long)fd, (long)buf, (long)len));
}

int open(const char *path, int flags, int mode) {
    return (int)errno_translate(syscall3(SYS_OPEN, (long)path, (long)flags, (long)mode));
}

int close(int fd) {
    return (int)errno_translate(syscall1(SYS_CLOSE, (long)fd));
}

void exit(int code) {
    syscall1(SYS_EXIT, (long)code);
    for (;;) {
    }
}

pid_t fork(void) {
    return (pid_t)errno_translate(syscall0(SYS_FORK));
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)errno_translate(syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp));
}

pid_t wait(int *status) {
    return waitpid((pid_t)WAIT_ANY, status, 0);
}

pid_t waitpid(pid_t pid, int *status, int options) {
    if (options != 0) {
        errno = EINVAL;
        return -1;
    }
    return wait_status(pid, status);
}

pid_t wait_status(pid_t pid, int *status) {
    return (pid_t)errno_translate(syscall2(SYS_WAIT, (long)(unsigned)pid, (long)status));
}

int pipe(int fds[2]) {
    return (int)errno_translate(syscall1(SYS_PIPE, (long)fds));
}

int dup(int oldfd) {
    return (int)errno_translate(syscall1(SYS_DUP, (long)oldfd));
}

int dup2(int oldfd, int newfd) {
    return (int)errno_translate(syscall2(SYS_DUP2, (long)oldfd, (long)newfd));
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)errno_translate(syscall3(SYS_LSEEK, (long)fd, (long)offset, (long)whence));
}

int fsync(int fd) {
    return (int)errno_translate(syscall1(SYS_FSYNC, (long)fd));
}

int mkdir(const char *path, mode_t mode) {
    return (int)errno_translate(syscall2(SYS_MKDIR, (long)path, (long)mode));
}

int unlink(const char *path) {
    return (int)errno_translate(syscall1(SYS_UNLINK, (long)path));
}

int rmdir(const char *path) {
    return (int)errno_translate(syscall1(SYS_RMDIR, (long)path));
}

int rename(const char *oldpath, const char *newpath) {
    return (int)errno_translate(syscall2(SYS_RENAME, (long)oldpath, (long)newpath));
}

int bigos_mkfs_bigfs(void) {
    return (int)errno_translate(syscall0(SYS_MKFS_BIGFS));
}

int stat(const char *path, struct stat *st) {
    return (int)errno_translate(syscall2(SYS_STAT, (long)path, (long)st));
}

int fstat(int fd, struct stat *st) {
    return (int)errno_translate(syscall2(SYS_FSTAT, (long)fd, (long)st));
}

int chdir(const char *path) {
    return (int)errno_translate(syscall1(SYS_CHDIR, (long)path));
}

char *getcwd(char *buf, size_t size) {
    long ret = syscall2(SYS_GETCWD, (long)buf, (long)size);
    if (ret < 0) {
        errno = (int)(-ret);
        return NULL;
    }
    return buf;
}

ssize_t bigos_readdir(int fd, struct bigos_dirent *entries, size_t max_entries) {
    return (ssize_t)errno_translate(syscall3(SYS_READDIR, (long)fd, (long)entries, (long)max_entries));
}

struct __bigos_DIR {
    int fd;
    size_t index;
    size_t count;
    struct bigos_dirent entries[BIGOS_DIRENT_MAX_BATCH];
    struct dirent current;
};

DIR *opendir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return NULL;
    if (!S_ISDIR(st.st_mode) && st.type != BIGOS_METADATA_TYPE_DIRECTORY) {
        errno = ENOTDIR;
        return NULL;
    }
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (dir == NULL) {
        int saved = errno;
        close(fd);
        errno = saved != 0 ? saved : ENOMEM;
        return NULL;
    }
    dir->fd = fd;
    dir->index = 0;
    dir->count = 0;
    memset(&dir->current, 0, sizeof(dir->current));
    return dir;
}

struct dirent *readdir(DIR *dir) {
    if (dir == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (dir->index >= dir->count) {
        ssize_t count = bigos_readdir(dir->fd, dir->entries, BIGOS_DIRENT_MAX_BATCH);
        if (count <= 0)
            return NULL;
        dir->count = (size_t)count;
        dir->index = 0;
    }
    struct bigos_dirent *entry = &dir->entries[dir->index++];
    dir->current.d_type = entry->type;
    strncpy(dir->current.d_name, entry->name, BIGOS_DIRENT_NAME_MAX);
    dir->current.d_name[BIGOS_DIRENT_NAME_MAX] = 0;
    return &dir->current;
}

int closedir(DIR *dir) {
    if (dir == NULL) {
        errno = EINVAL;
        return -1;
    }
    int fd = dir->fd;
    free(dir);
    return close(fd);
}

void *brk_raw(void *addr) {
    return (void *)syscall1(SYS_BRK, (long)addr);
}

void *mmap_anon(size_t len, long prot, long flags) {
    long ret = syscall3(SYS_MAP_ANON, (long)len, prot, flags);
    if (ret < 0) {
        errno = (int)(-ret);
        return MAP_FAILED;
    }
    return (void *)ret;
}

int bigos_munmap_anon(void *addr, size_t len) {
    return (int)errno_translate(syscall2(SYS_UNMAP_ANON, (long)addr, (long)len));
}

int bigos_mprotect_anon(void *addr, size_t len, long prot) {
    return (int)errno_translate(syscall3(SYS_PROTECT_ANON, (long)addr, (long)len, prot));
}

pid_t getpid(void) {
    return (pid_t)errno_translate(syscall0(SYS_GETPID));
}

pid_t getppid(void) {
    return (pid_t)errno_translate(syscall0(SYS_GETPPID));
}

int getuid(void) {
    return (int)errno_translate(syscall0(SYS_GETUID));
}

int getgid(void) {
    return (int)errno_translate(syscall0(SYS_GETGID));
}

int kill(pid_t pid, int signo) {
    return (int)errno_translate(syscall2(SYS_KILL, (long)pid, (long)signo));
}

long time_now(void) {
    return syscall0(SYS_GET_TIME);
}

time_t time(time_t *out) {
    time_t now = (time_t)syscall0(SYS_GET_TIME);
    if (out != NULL)
        *out = now;
    return now;
}

unsigned long get_tick(void) {
    return (unsigned long)syscall0(SYS_GET_TICK);
}

static int valid_user_signo(int signo) {
    return signo >= BIGOS_SIG_MIN && signo <= BIGOS_SIG_MAX;
}

static sigset_t signo_bit(int signo) {
    return (sigset_t)1ul << (signo - 1);
}

int sigemptyset(sigset_t *set) {
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = (sigset_t)((1ul << BIGOS_SIG_COUNT) - 1ul);
    return 0;
}

int sigaddset(sigset_t *set, int signo) {
    if (set == NULL || !valid_user_signo(signo)) {
        errno = EINVAL;
        return -1;
    }
    *set |= signo_bit(signo);
    return 0;
}

int sigdelset(sigset_t *set, int signo) {
    if (set == NULL || !valid_user_signo(signo)) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~signo_bit(signo);
    return 0;
}

int sigismember(const sigset_t *set, int signo) {
    if (set == NULL || !valid_user_signo(signo)) {
        errno = EINVAL;
        return -1;
    }
    return (*set & signo_bit(signo)) != 0;
}

void __bigos_signal_dispatch(int signo) {
    if (!valid_user_signo(signo))
        return;
    sighandler_t handler = g_signal_handlers[signo];
    if (handler != NULL && handler != SIG_DFL && handler != SIG_IGN)
        handler(signo);
}

int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact) {
    if (!valid_user_signo(signo)) {
        errno = EINVAL;
        return -1;
    }

    struct bigos_signal_disp raw_old;
    struct bigos_signal_disp *old_ptr = oldact != NULL ? &raw_old : NULL;
    unsigned long action = BIGOS_SIGACTION_DEFAULT;
    unsigned long handler = 0;
    sighandler_t user_handler = SIG_DFL;

    if (act != NULL) {
        if (act->sa_flags != 0) {
            errno = EINVAL;
            return -1;
        }
        user_handler = act->sa_handler;
        if (user_handler == SIG_DFL) {
            action = BIGOS_SIGACTION_DEFAULT;
            handler = 0;
        } else if (user_handler == SIG_IGN) {
            action = BIGOS_SIGACTION_IGNORE;
            handler = 0;
        } else {
            action = BIGOS_SIGACTION_HANDLER;
            handler = (unsigned long)__bigos_signal_trampoline;
        }
    }

    long ret = errno_translate(syscall4(SYS_SIGACTION, (long)signo, (long)action, (long)handler, (long)old_ptr));
    if (ret < 0)
        return -1;

    if (oldact != NULL) {
        oldact->sa_mask = 0;
        oldact->sa_flags = 0;
        if (raw_old.action == BIGOS_SIGACTION_IGNORE) {
            oldact->sa_handler = SIG_IGN;
        } else if (raw_old.action == BIGOS_SIGACTION_HANDLER) {
            oldact->sa_handler = g_signal_handlers[signo] != NULL ? g_signal_handlers[signo] : SIG_DFL;
        } else {
            oldact->sa_handler = SIG_DFL;
        }
    }
    if (act != NULL)
        g_signal_handlers[signo] = user_handler;
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    sigset_t requested = set != NULL ? *set : 0;
    return (int)errno_translate(syscall3(SYS_SIGPROCMASK, (long)how, (long)requested, (long)oldset));
}
