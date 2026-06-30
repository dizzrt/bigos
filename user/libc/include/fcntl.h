/* BigOS minimal fcntl constants.
 *
 * This header exposes only the open flags and fd-control commands implemented
 * by the bounded BigOS syscall wrappers. It does not claim full POSIX fcntl,
 * record locking, or async I/O support. It does expose a bounded O_NONBLOCK
 * subset: F_GETFL reports the access mode plus O_NONBLOCK, and F_SETFL toggles
 * only the O_NONBLOCK status flag on the open file description (pipe/tty/socket
 * read/write/recv return EWOULDBLOCK/EAGAIN instead of blocking). It is not full
 * POSIX status-flag handling. */
#ifndef _BIGOS_USER_FCNTL_H
#define _BIGOS_USER_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY (1 << 0)
#define O_RDWR   (1 << 1)
#define O_CREAT  (1 << 6)
#define O_TRUNC  (1 << 9)
/* Bounded nonblocking status flag. Mirrors the kernel bigos::vfs::OPEN_NONBLOCK
 * (1 << 11) and bigos::proc::O_NONBLOCK; does not collide with the flags above. */
#define O_NONBLOCK (1 << 11)

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

#define FD_CLOEXEC 1

int fcntl(int fd, int cmd, ...);

#endif /* _BIGOS_USER_FCNTL_H */
