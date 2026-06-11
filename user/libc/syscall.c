/* BigOS user libc: raw syscall primitives and wrappers.
 *
 * The kernel int 0x80 ABI: number in rax; args in rdi/rsi/rdx/r10/r8/r9; return
 * value in rax. The 4th arg uses r10 (not rcx). Wrappers translate a negative
 * kernel return into errno = -ret and a -1/sentinel result (POSIX convention).
 */
#include "libc.h"
#include "sys_nr.h"

int errno = 0;

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

pid_t wait(pid_t pid) {
    return (pid_t)errno_translate(syscall1(SYS_WAIT, (long)(unsigned)pid));
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

int mkdir(const char *path, int mode) {
    return (int)errno_translate(syscall2(SYS_MKDIR, (long)path, (long)mode));
}

int unlink(const char *path) {
    return (int)errno_translate(syscall1(SYS_UNLINK, (long)path));
}

void *brk_raw(void *addr) {
    return (void *)syscall1(SYS_BRK, (long)addr);
}

void *mmap_anon(size_t len, long prot, long flags) {
    long ret = syscall3(SYS_MAP_ANON, (long)len, prot, flags);
    if (ret < 0) {
        errno = (int)(-ret);
        return (void *)-1;
    }
    return (void *)ret;
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

unsigned long get_tick(void) {
    return (unsigned long)syscall0(SYS_GET_TICK);
}
