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

#ifdef BIGOS_SOCKET_SMOKE
    // Default-off kernel-internal socket closed-loop smoke. It builds a fake
    // network device, initializes the default context, and exercises the socket
    // backend object layer plus the bind/send/receive closed loop and error
    // paths through the same kernel-internal UDP API the socket syscalls use,
    // without requiring a real tap/network backend. Emits a deterministic
    // BIGOS_SOCKET_PASSED / BIGOS_SOCKET_FAILED marker.
    void socket_smoke_entry(void *) noexcept;
#endif
}   // namespace net
NAMESPACE_BIGOS_END

#endif   // _BIGOS_NET_SOCKET_H
