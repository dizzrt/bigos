/* BigOS minimal unistd declarations.
 *
 * These wrappers use the BigOS int 0x80 ABI and POSIX-style errno convention.
 * The header intentionally omits broad POSIX process, terminal, and filesystem
 * APIs that are not implemented by the current bounded userland. */
#ifndef _BIGOS_USER_UNISTD_H
#define _BIGOS_USER_UNISTD_H

#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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
int mkdir(const char *path, mode_t mode);
int unlink(const char *path);
int rename(const char *oldpath, const char *newpath);
int bigos_mkfs_bigfs(void);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
void *brk_raw(void *addr); /* returns committed break, or (void*)-errno */
void *mmap_anon(size_t len, long prot, long flags);
pid_t getpid(void);
pid_t getppid(void);
int getuid(void);
int getgid(void);
int kill(pid_t pid, int signo);
long time_now(void);
unsigned long get_tick(void);

#endif /* _BIGOS_USER_UNISTD_H */
