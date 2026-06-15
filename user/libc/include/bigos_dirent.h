#ifndef _BIGOS_USER_BIGOS_DIRENT_H
#define _BIGOS_USER_BIGOS_DIRENT_H

#include <sys/types.h>

#define BIGOS_DIRENT_NAME_MAX 27
#define BIGOS_DIRENT_TYPE_FILE 1
#define BIGOS_DIRENT_TYPE_DIRECTORY 2
#define BIGOS_DIRENT_MAX_BATCH 16

struct bigos_dirent {
    unsigned int type;
    char name[BIGOS_DIRENT_NAME_MAX + 1];
};

typedef struct __bigos_DIR DIR;

struct dirent {
    unsigned int d_type;
    char d_name[BIGOS_DIRENT_NAME_MAX + 1];
};

ssize_t bigos_readdir(int fd, struct bigos_dirent *entries, size_t max_entries);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif /* _BIGOS_USER_BIGOS_DIRENT_H */
