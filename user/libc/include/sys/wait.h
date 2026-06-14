/* BigOS bounded wait constants and wrapper declarations.
 *
 * wait()/waitpid() expose the Stage 39 POSIX-like shape over the existing
 * SYS_WAIT ABI. Only options == 0 is supported: no WNOHANG, process groups,
 * job control, stopped/continued state, or complete POSIX status encoding. */
#ifndef _BIGOS_USER_SYS_WAIT_H
#define _BIGOS_USER_SYS_WAIT_H

#include <sys/types.h>

#define WAIT_ANY 0xffffffffu

#define WIFEXITED(status)   ((status) >= 0)
#define WEXITSTATUS(status) (status)
#define WIFSIGNALED(status) ((status) < 0)
#define WTERMSIG(status)    (-(status)-128)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
/* BigOS-specific raw wait shape: pid/WAIT_ANY plus raw bounded status output. */
pid_t wait_status(pid_t pid, int *status);

#endif /* _BIGOS_USER_SYS_WAIT_H */
