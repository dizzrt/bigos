/* BigOS minimal wait constants and wrapper declarations. */
#ifndef _BIGOS_USER_SYS_WAIT_H
#define _BIGOS_USER_SYS_WAIT_H

#include <sys/types.h>

/* Wait for any child (mirrors bigos::proc::WAIT_ANY). */
#define WAIT_ANY 0xffffffffu

pid_t wait(pid_t pid);

#endif /* _BIGOS_USER_SYS_WAIT_H */
