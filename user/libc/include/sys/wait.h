/* BigOS bounded wait constants and wrapper declarations.
 *
 * wait()/waitpid() expose the bounded POSIX-like shape over the BigOS wait
 * syscalls. Only WAIT_ANY, positive child pid selectors, and WNOHANG are
 * supported: no process groups, job control, stopped/continued state, resource
 * usage, or complete POSIX status encoding. */
#ifndef _BIGOS_USER_SYS_WAIT_H
#define _BIGOS_USER_SYS_WAIT_H

#include <sys/types.h>

#define WAIT_ANY 0xffffffffu
#define WNOHANG  1

#define WIFEXITED(status)   ((status) >= 0)
#define WEXITSTATUS(status) (status)
#define WIFSIGNALED(status) ((status) < 0)
#define WTERMSIG(status)    (-(status)-128)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
/* BigOS-specific raw wait shape: pid/WAIT_ANY plus raw bounded status output. */
pid_t wait_status(pid_t pid, int *status);

#endif /* _BIGOS_USER_SYS_WAIT_H */
