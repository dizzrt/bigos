/* BigOS minimal user-space socket declarations.
 *
 * Bounded BigOS sockaddr-lite for IPv4 + port only, in host byte order, mirroring
 * bigos::sys::SockAddrIn in include/bigos/syscall.h. These wrappers use the BigOS
 * int 0x80 ABI and POSIX-style errno convention. This is intentionally NOT a full
 * POSIX sys/socket.h: it supports the bounded UDP datagram subset and the bounded
 * TCP stream subset only (connect/listen/accept + read/write/send), with no full
 * AF_/SOCK_ family matrix, no shutdown/getpeername/getsockname/accept4, getsockopt
 * limited to SOL_SOCKET/SO_ERROR, no setsockopt, no scatter-gather, and no
 * ancillary data. recvfrom uses a bounded advance plus bounded wait rather than
 * general POSIX blocking. */
#ifndef _BIGOS_USER_SYS_SOCKET_H
#define _BIGOS_USER_SYS_SOCKET_H

#include <sys/types.h>

/* Bounded domain/type/protocol subset accepted by socket(). */
#define AF_INET      2
#define SOCK_DGRAM   2
#define SOCK_STREAM  1
#define IPPROTO_UDP  17
#define IPPROTO_TCP  6

/* Bounded getsockopt: ONLY SOL_SOCKET/SO_ERROR is supported. */
#define SOL_SOCKET   1
#define SO_ERROR     4

/* Bounded send() flag: MSG_NOSIGNAL suppresses SIGPIPE on a broken-pipe write. */
#define MSG_NOSIGNAL 0x4000

typedef unsigned int socklen_t;

/* Fixed-size IPv4 + port address, host byte order. Layout MUST match
 * bigos::sys::SockAddrIn (family, port, addr; all unsigned, host order). */
struct sockaddr_in {
    unsigned short sin_family; /* AF_INET */
    unsigned short sin_port;   /* UDP port, host order */
    unsigned int sin_addr;     /* IPv4 address, host order */
};

/* Builds a host-order IPv4 address from dotted-quad octets. */
static inline unsigned int bigos_ipv4(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    return ((unsigned int)a << 24) | ((unsigned int)b << 16) | ((unsigned int)c << 8) | (unsigned int)d;
}

/* Creates a bounded UDP socket. Returns an fd or -1 with errno set. */
int socket(int domain, int type, int protocol);

/* Binds a UDP socket fd to a local port. addrlen MUST equal
 * sizeof(struct sockaddr_in). Returns 0 or -1 with errno set. */
int bind(int fd, const struct sockaddr_in *addr, socklen_t addrlen);

/* Sends a bounded UDP datagram to dst. Returns bytes sent or -1 with errno. */
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr_in *dst, socklen_t addrlen);

/* Receives one UDP datagram. When src is non-null *addrlen must equal
 * sizeof(struct sockaddr_in) on entry. Returns bytes received, or -1 with errno
 * (EAGAIN when no datagram arrived within the bounded wait). */
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr_in *src, socklen_t *addrlen);

/* Active-open a stream (TCP) socket to addr. Blocking fd blocks to ESTABLISHED;
 * nonblocking fd returns -1 with errno=EINPROGRESS. addrlen MUST equal
 * sizeof(struct sockaddr_in). Returns 0 or -1 with errno set. */
int connect(int fd, const struct sockaddr_in *addr, socklen_t addrlen);

/* Mark a bound stream socket passive. backlog is clamped to the bounded accept
 * queue capacity. Returns 0 or -1 with errno set. */
int listen(int fd, int backlog);

/* Take one completed connection off a listening stream socket. When peer is
 * non-null *addrlen must equal sizeof(struct sockaddr_in) on entry and receives
 * the peer address. Returns a new connection fd, or -1 with errno (EAGAIN on a
 * nonblocking socket with no pending connection). */
int accept(int fd, struct sockaddr_in *peer, socklen_t *addrlen);

/* Bounded getsockopt: ONLY level==SOL_SOCKET, optname==SO_ERROR is supported and
 * reads-and-clears the connection's pending error. Any other combination fails
 * with errno=ENOPROTOOPT. Returns 0 or -1 with errno set. */
int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen);

/* Stream socket send. Data path equals write; flags recognizes only MSG_NOSIGNAL
 * (suppress SIGPIPE on a broken-pipe write). Any other flag bit fails with
 * errno=EINVAL. Returns bytes sent or -1 with errno set. */
ssize_t send(int fd, const void *buf, size_t len, int flags);

#endif /* _BIGOS_USER_SYS_SOCKET_H */
