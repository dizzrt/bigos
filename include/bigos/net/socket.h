#ifndef _BIGOS_NET_SOCKET_H
#define _BIGOS_NET_SOCKET_H

#include <bigos/types.h>
#include <bigos/fs/vfs.h>
#include <bigos/net.h>

NAMESPACE_BIGOS_BEG
namespace net {
    // Minimal user-visible UDP socket backend. Each socket is a vfs::File whose
    // ops table is SOCKET_OPS and whose private_data is a Socket state object
    // holding the owning protocol Context and (once bound) its UdpEndpoint. The
    // socket fd reuses every existing fd-table path (install/dup/dup2/close/
    // close-on-exec/fork retain/release); vfs::release on the last reference runs
    // SOCKET_OPS.close which recycles the endpoint exactly once.
    //
    // This is a bounded adapter over the kernel-internal bigos::net UDP API. It
    // does not claim full POSIX socket semantics: no TCP/stream, no AF_* matrix,
    // no connect/listen/accept, no option matrix, and recvfrom uses a bounded
    // poll-and-yield advance rather than general POSIX blocking.

    struct Socket {
        Context *context;       // owning protocol context (single default_context)
        UdpEndpoint *endpoint;  // non-null once bound to a local port
        uint16_t local_port;    // bound local port, 0 when unbound
        bool bound;
    };

    // Forward declaration: the stream socket backend holds a kernel-internal TCP
    // connection handle. The full TcpControlBlock layout lives in bigos/net/tcp.h;
    // socket.h only needs the pointer type here.
    struct TcpControlBlock;

    // Minimal user-visible TCP stream socket backend. Each stream socket is a
    // vfs::File whose ops table is STREAM_SOCKET_OPS and whose private_data is a
    // StreamSocket holding the owning protocol Context and (once listening/
    // connecting/connected) its TcpControlBlock. It reuses every existing fd-table
    // path exactly like the UDP socket; vfs::release on the last reference runs
    // STREAM_SOCKET_OPS.close which tears the connection down and recycles the TCB.
    //
    // This is a bounded adapter over the kernel-internal bigos::net tcp_* API. It
    // is not full POSIX socket semantics: only SOCK_STREAM + TCP over IPv4, no
    // setsockopt/shutdown/getpeername/getsockname/accept4 matrix, getsockopt only
    // supports SOL_SOCKET/SO_ERROR, and no name resolution.
    struct StreamSocket {
        enum class Role : uint8_t {
            Unbound = 0,   // created, not bound / not connected
            Bound,         // local port reserved for a future listen (bind recorded)
            Listening,     // tcb is a LISTEN control block
            Connecting,    // active open in progress (SynSent, nonblocking connect)
            Connected,     // tcb is an Established (or accepted) connection
            Closed,        // connection torn down
        };
        Context *context;          // owning protocol context (single default_context)
        TcpControlBlock *tcb;      // non-null once listen/connect/accept associates one
        uint16_t local_port;       // bound/listen local port, 0 when unbound
        Role role;
        // Pending connection error for getsockopt(SO_ERROR), aligned with Linux/BSD
        // nonblocking connect completion. 0 means no pending error. Positive POSIX
        // errno value (e.g. ECONNREFUSED); read-and-cleared by getsockopt.
        int32_t pending_error;
        // Generation of the associated TCB slot at association time. Paired with
        // tcp_connection_alive() to detect that the bounded TCB slot was recycled
        // (connection reset/torn down) out from under this socket.
        uint32_t tcb_generation;
    };

    // Creates an unbound UDP socket File. On success *__out_file owns one
    // reference (ref_count == 1) and Status::Success is returned. On allocation
    // failure nothing is published and Status::NoMemory is returned. The socket
    // is created against the supplied context (the default_context()). Non-IRQ /
    // blockable context only.
    bigos::vfs::Status socket_create(Context *__context, bigos::vfs::File **__out_file) noexcept;

    // True when the file object is a UDP socket backend (ops identity check).
    bool is_socket_file(const bigos::vfs::File *__file) noexcept;

    // Returns the Socket state behind a socket File, or nullptr when __file is
    // not a socket. The reference count is not changed.
    Socket *socket_state(bigos::vfs::File *__file) noexcept;

    // Creates an unbound stream (TCP) socket File. On success *__out_file owns one
    // reference (ref_count == 1). Allocation failure publishes nothing and returns
    // Status::NoMemory. Blockable (non-IRQ) context only.
    bigos::vfs::Status stream_socket_create(Context *__context, bigos::vfs::File **__out_file) noexcept;

    // Wraps an already-Established/accepted TCB into a new stream socket File in the
    // Connected role. Used by the accept path to publish a freshly accepted
    // connection under a new fd. On allocation failure nothing is published and
    // Status::NoMemory is returned (the caller keeps ownership of the TCB).
    bigos::vfs::Status stream_socket_create_accepted(
        Context *__context, TcpControlBlock *__tcb, uint16_t __local_port, bigos::vfs::File **__out_file) noexcept;

    // True when the file object is a stream socket backend (ops identity check).
    bool is_stream_socket_file(const bigos::vfs::File *__file) noexcept;

    // Returns the StreamSocket state behind a stream socket File, or nullptr when
    // __file is not a stream socket. The reference count is not changed.
    StreamSocket *stream_socket_state(bigos::vfs::File *__file) noexcept;

    // Bounded stream socket send with a MSG_NOSIGNAL suppress flag, backing
    // SYS_SEND. Data path is identical to the stream socket write op; when the
    // connection's write direction is closed it returns -EPIPE and, unless
    // __suppress_sigpipe (or a process SIG_IGN on SIGPIPE), delivers SIGPIPE via
    // the unified broken-pipe helper. Returns bytes accepted (>=0) or a negative
    // errno. Blockable (non-IRQ) context only.
    int64_t stream_socket_send(
        bigos::vfs::File *__file, const void *__buf, size_t __len, bool __suppress_sigpipe) noexcept;

#ifdef BIGOS_SOCKET_SMOKE
    // Default-off kernel-internal socket closed-loop smoke. It builds a fake
    // network device, initializes the default context, and exercises the socket
    // backend object layer plus the bind/send/receive closed loop and error
    // paths through the same kernel-internal UDP API the socket syscalls use,
    // without requiring a real tap/network backend. Emits a deterministic
    // BIGOS_SOCKET_PASSED / BIGOS_SOCKET_FAILED marker.
    void socket_smoke_entry(void *) noexcept;
#endif

#ifdef BIGOS_STREAM_SOCKET_SMOKE
    // Default-off kernel-internal / user-visible stream socket closed-loop smoke.
    // On a LoopbackReady context (local config, no frame-level device) it drives
    // socket(SOCK_STREAM)->bind->listen->accept plus connect, ordered read/write/
    // send bidirectional exchange with short writes, EOF, poll readiness,
    // nonblocking -EINPROGRESS/-EAGAIN, nonblocking connect + getsockopt(SO_ERROR),
    // write to a closed peer (SIGPIPE + -EPIPE), send(MSG_NOSIGNAL) suppression,
    // send unknown-flags -EINVAL, accept queue full, connection reset (-ECONNRESET),
    // and non-stream/unconnected/illegal-argument rejection, emitting a
    // deterministic BIGOS_STREAM_SOCKET_PASSED / BIGOS_STREAM_SOCKET_FAILED marker.
    void stream_socket_smoke_entry(void *) noexcept;
#endif
}   // namespace net
NAMESPACE_BIGOS_END

#endif   // _BIGOS_NET_SOCKET_H
