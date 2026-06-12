/* BigOS minimal fcntl constants.
 *
 * This header exposes only the open flags implemented by the current VFS
 * syscall wrappers. It does not claim full POSIX fcntl support. */
#ifndef _BIGOS_USER_FCNTL_H
#define _BIGOS_USER_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY (1 << 0)
#define O_RDWR   (1 << 1)
#define O_CREAT  (1 << 6)
#define O_TRUNC  (1 << 9)

#endif /* _BIGOS_USER_FCNTL_H */
