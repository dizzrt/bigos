#ifndef _BIGOS_USER_SYS_STAT_H
#define _BIGOS_USER_SYS_STAT_H

#include <sys/types.h>

#define BIGOS_METADATA_TYPE_UNKNOWN   0u
#define BIGOS_METADATA_TYPE_REGULAR   1u
#define BIGOS_METADATA_TYPE_DIRECTORY 2u

#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(m) (((m) & 0170000) == S_IFREG)
#define S_ISDIR(m) (((m) & 0170000) == S_IFDIR)

/* BigOS bounded metadata subset, not a complete POSIX struct stat. */
struct stat {
    unsigned int type;
    mode_t st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int st_nlink;
    unsigned int reserved0;
    unsigned long st_size;
    unsigned long st_object_id; /* first ABI version always returns zero */
    unsigned long reserved[4];
};

int mkdir(const char *path, mode_t mode);
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);

#endif /* _BIGOS_USER_SYS_STAT_H */
