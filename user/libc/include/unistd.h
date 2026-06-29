/* BigOS minimal unistd declarations.
 *
 * These wrappers use the BigOS int 0x80 ABI and POSIX-style errno convention.
 * The header intentionally omits broad POSIX process, terminal, and filesystem
 * APIs that are not implemented by the current bounded userland. Process
 * group/session and terminal foreground helpers are BigOS bounded subsets, and
 * sleep wrappers are coarse tick-based waits, not complete POSIX job-control,
 * termios, timer, or signal-interruptible sleep support. */
#ifndef _BIGOS_USER_UNISTD_H
#define _BIGOS_USER_UNISTD_H

#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

ssize_t write(int fd, const void *buf, size_t len);
ssize_t read(int fd, void *buf, size_t len);
int open(const char *path, int flags, int mode);
int close(int fd);
void exit(int code) __attribute__((noreturn));
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t wait_status(pid_t pid, int *status);
int pipe(int fds[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
off_t lseek(int fd, off_t offset, int whence);
int fsync(int fd);
/* Bounded BigOS writable-backend sync; not full POSIX sync(2) or fdatasync. */
int sync(void);
int access(const char *path, int mode);
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);
int bigos_utimens(const char *path, long atime, long mtime, unsigned int flags);
int mkdir(const char *path, mode_t mode);
int unlink(const char *path);
int rmdir(const char *path);
int rename(const char *oldpath, const char *newpath);
int bigos_mkfs_bigfs(void);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
void *brk_raw(void *addr); /* returns committed break, or (void*)-errno */
void *mmap_anon(size_t len, long prot, long flags);
int bigos_munmap_anon(void *addr, size_t len);
int bigos_mprotect_anon(void *addr, size_t len, long prot);
pid_t getpid(void);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
pid_t getsid(pid_t pid);
int setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgid);
/* Returns 1 when fd refers to the terminal (character device), 0 otherwise
 * (regular file, pipe, or invalid fd). Implemented over fstat; not full
 * POSIX isatty (no ENOTTY/EBADF distinction beyond the 0 return). */
int isatty(int fd);
int getuid(void);
int getgid(void);
int kill(pid_t pid, int signo);
long time_now(void);
unsigned long get_tick(void);
int bigos_sleep_ms(unsigned long milliseconds);
unsigned int sleep(unsigned int seconds);

#endif /* _BIGOS_USER_UNISTD_H */
