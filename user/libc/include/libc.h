/* BigOS minimal user-space libc public header.
 *
 * Freestanding C declarations for the user runtime: syscall wrappers (POSIX
 * errno convention), minimal string/memory routines, a bounded brk-based
 * malloc/free, minimal stdio/printf, and read-only environ/getenv. The libc
 * interacts with the kernel only through int 0x80 (see sys_nr.h). It does NOT
 * implement full POSIX semantics. */
#ifndef _BIGOS_LIBC_H
#define _BIGOS_LIBC_H

#include <errno.h> /* IWYU pragma: export */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef int pid_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Open flags / seek whence mirror include/bigos/fs/vfs.h numeric layout. */
#define O_RDONLY 0
#define O_WRONLY (1 << 0)
#define O_RDWR   (1 << 1)
#define O_CREAT  (1 << 6)
#define O_TRUNC  (1 << 9)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Wait for any child (mirrors bigos::proc::WAIT_ANY). */
#define WAIT_ANY 0xffffffffu

/* --- raw syscall primitives (rax=number, rdi/rsi/rdx/r10/r8/r9) --- */
long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall4(long n, long a0, long a1, long a2, long a3);
long syscall5(long n, long a0, long a1, long a2, long a3, long a4);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

/* --- syscall wrappers (POSIX errno convention unless noted) --- */
ssize_t write(int fd, const void *buf, size_t len);
ssize_t read(int fd, void *buf, size_t len);
int open(const char *path, int flags, int mode);
int close(int fd);
void exit(int code) __attribute__((noreturn));
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t wait(pid_t pid);
int pipe(int fds[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
off_t lseek(int fd, off_t offset, int whence);
int fsync(int fd);
int mkdir(const char *path, int mode);
int unlink(const char *path);
void *brk_raw(void *addr); /* returns committed break, or (void*)-errno */
void *mmap_anon(size_t len, long prot, long flags);
pid_t getpid(void);
pid_t getppid(void);
int getuid(void);
int getgid(void);
int kill(pid_t pid, int signo);
long time_now(void);
unsigned long get_tick(void);

/* --- string / memory --- */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);

/* --- heap --- */
void *malloc(size_t n);
void free(void *p);

/* --- stdio --- */
int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);

/* --- environment (read-only) --- */
extern char **environ;
char *getenv(const char *name);

#endif /* _BIGOS_LIBC_H */
