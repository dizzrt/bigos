/* BigOS minimal user-space poll(2) declarations.
 *
 * Bounded multiplexing mirror of bigos::sys::pollfd and the POLL* event bits in
 * include/bigos/syscall.h. The struct layout and event bit values MUST match the
 * kernel header; a source-contract test under tests/ asserts equality. These
 * wrappers use the BigOS int 0x80 ABI and POSIX-style errno convention.
 *
 * This is intentionally NOT full POSIX poll(2): the descriptor set is
 * fixed-capacity (POLL_MAX_FDS, rejected with EINVAL past the bound), readiness
 * is level-triggered, and there is no ppoll, no POLLPRI/out-of-band, no
 * POLLRDHUP, no edge-triggered/event-object semantics, and no EINTR restart
 * beyond the underlying bounded blocking primitive. */
#ifndef _BIGOS_USER_POLL_H
#define _BIGOS_USER_POLL_H

/* Fixed-size descriptor entry. Layout MUST match bigos::sys::pollfd
 * (fd:int32, events:uint16, revents:uint16). */
struct pollfd {
    int fd;                 /* watched descriptor; negative to ignore this entry */
    unsigned short events;  /* requested event bits (POLLIN/POLLOUT subset) */
    unsigned short revents; /* kernel-filled ready bits */
};

/* Bounded poll event bits, aligned with common Linux poll(2) values.
 * POLLERR/POLLHUP/POLLNVAL are output only. */
#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020

/* Fixed capacity of a single descriptor set; nfds beyond it fails with EINVAL. */
#define POLL_MAX_FDS 16

/* Waits until a descriptor in fds is ready, the timeout expires, or a
 * deterministic error. timeout < 0 waits without a deadline; timeout == 0 is a
 * non-blocking probe; timeout > 0 is a millisecond bound. Returns the number of
 * ready descriptors (0 on timeout), or -1 with errno set. */
int poll(struct pollfd *fds, unsigned long nfds, int timeout);

#endif /* _BIGOS_USER_POLL_H */
