/* BigOS user-space syscall number mirror header.
 *
 * Mirror of bigos::sys::SyscallNumber in include/bigos/syscall.h. The values
 * MUST match the kernel header exactly; a source-contract test under tests/
 * asserts equality so a kernel renumbering that misses this mirror fails fast.
 * This header is plain C and freestanding: it does not include any kernel C++
 * header, keeping kernel ABI details out of the user C compile. */
#ifndef _BIGOS_USER_SYS_H
#define _BIGOS_USER_SYS_H

#define SYS_DEBUG_WRITE 0
#define SYS_GET_TICK    1
#define SYS_WRITE       2
#define SYS_EXIT        3
#define SYS_WAIT        4
#define SYS_OPEN        5
#define SYS_READ        6
#define SYS_CLOSE       7
#define SYS_BRK         8
#define SYS_MAP_ANON    9
#define SYS_FORK        10
#define SYS_GET_TIME    11
#define SYS_GETPID      12
#define SYS_GETPPID     13
#define SYS_GETUID      14
#define SYS_GETGID      15
#define SYS_KILL        16
#define SYS_SIGACTION   17
#define SYS_SIGPROCMASK 18
#define SYS_SIGRETURN   19
#define SYS_LSEEK       20
#define SYS_PIPE        21
#define SYS_DUP         22
#define SYS_DUP2        23
#define SYS_FSYNC       24
#define SYS_MKDIR       25
#define SYS_UNLINK      26
#define SYS_EXECVE      27
#define SYS_READDIR     28
#define SYS_STAT        29
#define SYS_FSTAT       30
#define SYS_CHDIR       31
#define SYS_GETCWD      32
#define SYS_RENAME      33
#define SYS_MKFS_BIGFS  34
#define SYS_MAP_FILE    35
#define SYS_UNMAP_ANON  36
#define SYS_PROTECT_ANON 37
#define SYS_RMDIR       38
#define SYS_FTRUNCATE   39
#define SYS_SYNC        40
#define SYS_GETPGID     41
#define SYS_GETSID      42
#define SYS_SETPGID     43
#define SYS_SETSID      44
#define SYS_TCGETPGRP   45
#define SYS_TCSETPGRP   46
#define SYS_WAITPID     47
#define SYS_FCNTL       48
#define SYS_ACCESS      49
#define SYS_TRUNCATE    50
#define SYS_TCGETMODE   51
#define SYS_TCSETMODE   52
#define SYS_SLEEP_MS    53
#define SYS_UTIMENS     54
#define SYS_SOCKET      55
#define SYS_BIND        56
#define SYS_SENDTO      57
#define SYS_RECVFROM    58
#define SYS_DYN_MAP     59
#define SYS_DYN_PROTECT 60
#define SYS_POLL        61

#endif /* _BIGOS_USER_SYS_H */
