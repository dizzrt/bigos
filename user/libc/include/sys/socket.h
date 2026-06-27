/* BigOS minimal user-space UDP socket declarations.
 *
 * Bounded BigOS sockaddr-lite for IPv4 + port only, in host byte order, mirroring
 * bigos::sys::SockAddrIn in include/bigos/syscall.h. These wrappers use the BigOS
 * int 0x80 ABI and POSIX-style errno convention. This is intentionally NOT a full
 * POSIX sys/socket.h: there is no TCP/stream support, no full AF_ or SOCK_ family
 * matrix, no connect/listen/accept/shutdown, no getsockopt/setsockopt, no poll or
 * select, no scatter-gather, and no ancillary data. recvfrom uses a bounded
 * advance plus bounded wait rather than general POSIX blocking. */
#ifndef _BIGOS_USER_SYS_SOCKET_H
#define _BIGOS_USER_SYS_SOCKET_H

#include <sys/types.h>

/* Bounded domain/type/protocol subset accepted by socket(). */
#define AF_INET      2
#define SOCK_DGRAM   2
#define IPPROTO_UDP  17

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

#endif /* _BIGOS_USER_SYS_SOCKET_H */
