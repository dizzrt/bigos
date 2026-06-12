#ifndef _BIGOS_USER_SYS_STAT_H
#define _BIGOS_USER_SYS_STAT_H

#include <sys/types.h>

#define S_IFREG 0100000
#define S_IFDIR 0040000

int mkdir(const char *path, mode_t mode);

#endif /* _BIGOS_USER_SYS_STAT_H */
