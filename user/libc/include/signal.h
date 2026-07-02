/* BigOS bounded signal declarations.
 *
 * This is a minimal POSIX-like surface backed by the existing BigOS signal
 * syscalls. It intentionally omits siginfo, alternate stacks, realtime signals,
 * queued delivery, complete job control, termios, and complete POSIX semantics. */
#ifndef _BIGOS_USER_SIGNAL_H
#define _BIGOS_USER_SIGNAL_H

#include <sys/types.h>

#define SIGINT 2
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGTERM 15
#define SIGCHLD 17

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef unsigned long sigset_t;
typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask; /* Reserved in this bounded stage; same-signo is masked. */
    int sa_flags;     /* Must be zero; no SA_* flags are implemented. */
};

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

#endif /* _BIGOS_USER_SIGNAL_H */
