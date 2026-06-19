/* BigOS minimal fcntl constants.
 *
 * This header exposes only the open flags and fd-control commands implemented
 * by the bounded BigOS syscall wrappers. It does not claim full POSIX fcntl,
 * record locking, nonblocking I/O, or async I/O support. */
#ifndef _BIGOS_USER_FCNTL_H
#define _BIGOS_USER_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY (1 << 0)
#define O_RDWR   (1 << 1)
#define O_CREAT  (1 << 6)
#define O_TRUNC  (1 << 9)

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2

#define FD_CLOEXEC 1

int fcntl(int fd, int cmd, ...);

#endif /* _BIGOS_USER_FCNTL_H */
