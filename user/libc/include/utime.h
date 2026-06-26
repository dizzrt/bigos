/* BigOS bounded file timestamp declarations.
 *
 * Exposes second-resolution path timestamp updates only. This is not complete
 * POSIX utimensat/futimens/lutimes, nanosecond precision, symlink timestamp
 * mutation, timezone conversion, or locale formatting.
 */
#ifndef _BIGOS_USER_UTIME_H
#define _BIGOS_USER_UTIME_H

#include <time.h>

#define BIGOS_UTIME_ATIME_NOW  (1u << 0)
#define BIGOS_UTIME_MTIME_NOW  (1u << 1)
#define BIGOS_UTIME_ATIME_OMIT (1u << 2)
#define BIGOS_UTIME_MTIME_OMIT (1u << 3)

struct utimbuf {
    time_t actime;
    time_t modtime;
};

int bigos_utimens(const char *path, time_t atime, time_t mtime, unsigned int flags);
int utime(const char *path, const struct utimbuf *times);

#endif /* _BIGOS_USER_UTIME_H */
