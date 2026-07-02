#include <bigos/net/socket.h>

#include <bigos/errno.h>
#include <bigos/memory.h>
#include <bigos/net/tcp.h>
#include <bigos/proc.h>
#include <bigos/sched.h>
#include <bigos/signal.h>
#if defined(BIGOS_SOCKET_SMOKE) || defined(BIGOS_STREAM_SOCKET_SMOKE)
#include <bigos/device.h>
#include <bigos/io.h>
#include <string.h>
#endif

namespace {
    // Read/write on a connectionless UDP socket fd are intentionally unsupported:
    // a datagram socket has no implicit peer, so send/receive go only through
    // sendto/recvfrom. read/write therefore return a deterministic unsupported
    // error rather than performing any implicit addressing.
    bigos::vfs::Status socket_read(
        bigos::vfs::File *__file, void *, size_t, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::Status::Unsupported;
    }

    bigos::vfs::Status socket_write(
        bigos::vfs::File *__file, const void *, size_t, size_t *__bytes_written) noexcept {
        if (__bytes_written != nullptr)
            *__bytes_written = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::Status::Unsupported;
    }

    bigos::vfs::Status socket_lseek(
        bigos::vfs::File *, int64_t, int, uint64_t *) noexcept {
        return bigos::vfs::Status::NotSeekable;
    }

    // Read-only readiness snapshot for a UDP socket. A bound, active endpoint with
    // a non-empty receive queue is readable and (being able to send) writable; an
    // unbound or inactive endpoint is reported as an error. It dequeues nothing
    // and changes no socket state.
    uint32_t socket_poll(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::READY_ERROR;
        bigos::net::Socket *socket = (bigos::net::Socket *)__file->private_data;
        if (!socket->bound || socket->endpoint == nullptr || !socket->endpoint->active)
            return bigos::vfs::READY_ERROR;
        uint32_t ready = bigos::vfs::READY_WRITABLE;
        if (socket->endpoint->rx_count > 0)
            ready |= bigos::vfs::READY_READABLE;
        return ready;
    }

    // Recycles the protocol endpoint (if bound) and frees the Socket state on the
    // last reference. vfs::release guarantees this runs exactly once when the
    // final fd referencing the File is closed, so there is no double free or
    // leak across close/fork/error-rollback paths.
    void socket_close(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr || __file->private_data == nullptr)
            return;
        bigos::net::Socket *socket = (bigos::net::Socket *)__file->private_data;
        if (socket->bound && socket->endpoint != nullptr && socket->context != nullptr)
            (void)bigos::net::udp_close(socket->context, socket->endpoint);
        bigos::free(socket);
        __file->private_data = nullptr;
    }

    // Read-only readiness wait-queue op. A bound, active endpoint contributes its
    // receive wait queue (woken by the protocol RX delivery path when a datagram
    // arrives). An unbound or inactive endpoint contributes no queue: its
    // poll_file() already reports READY_ERROR, so a multiplexing wait treats it as
    // immediately ready and never blocks on it. Returns queue handles only and
    // changes no socket state.
    uint32_t socket_poll_wait(
        bigos::vfs::File *__file, uint32_t, bigos::sched::WaitQueue **__out, uint32_t __max) noexcept {
        if (__file == nullptr || __file->private_data == nullptr || __out == nullptr || __max == 0)
            return 0;
        bigos::net::Socket *socket = (bigos::net::Socket *)__file->private_data;
        if (!socket->bound || socket->endpoint == nullptr || !socket->endpoint->active)
            return 0;
        __out[0] = &socket->endpoint->rx_wait;
        return 1;
    }

    const bigos::vfs::FileOperations SOCKET_OPS = {
        &socket_read, &socket_close, &socket_write, &socket_lseek, nullptr, nullptr, &socket_poll, &socket_poll_wait};

    // ---- Stream (TCP) socket backend ---------------------------------------
    //
    // A stream socket's private_data is a StreamSocket. read/write route to the
    // kernel-internal tcp_receive/tcp_send once a connection is associated, and the
    // would-block/EOF/error decisions are shared with poll (stream_poll below).
    // Blocking is bounded and ordinary-context: it advances the protocol with
    // net::pump + tcp_pump and waits on the connection-level wait queue.

    constexpr uint32_t STREAM_RECV_PUMP_FRAMES = 4;   // bounded frames per pump round

    // True when the socket's associated TCB was recycled/reset out from under it
    // (connection gone). A Connecting/Connected socket whose TCB is no longer alive
    // has been reset.
    bool stream_tcb_alive(const bigos::net::StreamSocket *__s) noexcept {
        return __s->tcb != nullptr && bigos::net::tcp_connection_alive(__s->tcb, __s->tcb_generation);
    }

    // Advance the protocol path once (RX + retransmit/timeout). Ordinary context.
    void stream_pump(bigos::net::StreamSocket *__s) noexcept {
        if (__s->context == nullptr)
            return;
        (void)bigos::net::pump(__s->context, STREAM_RECV_PUMP_FRAMES);
        (void)bigos::net::tcp_pump(__s->context);
    }

    bigos::vfs::Status stream_read(
        bigos::vfs::File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        constexpr uint32_t READ_MAX_ROUNDS = 16;
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        bigos::net::StreamSocket *s = (bigos::net::StreamSocket *)__file->private_data;
        if (__len == 0)
            return bigos::vfs::Status::Success;
        if (__dst == nullptr)
            return bigos::vfs::Status::InvalidArgument;
        if (!bigos::sched::can_block())
            return bigos::vfs::Status::WouldBlock;
        if (s->role != bigos::net::StreamSocket::Role::Connected || s->tcb == nullptr)
            return bigos::vfs::Status::NotConnected;

        const bool nonblocking = bigos::vfs::file_is_nonblocking(__file);
        const uint32_t rounds = nonblocking ? 1u : READ_MAX_ROUNDS;
        for (uint32_t round = 0; round < rounds; round++) {
            if (!stream_tcb_alive(s))
                return bigos::vfs::Status::ConnectionReset;
            uint32_t got = 0;
            const bigos::net::Status st =
                bigos::net::tcp_receive(s->tcb, (uint8_t *)__dst, (uint32_t)__len, &got);
            if (st == bigos::net::Status::Ok && got != 0) {
                if (__bytes_read != nullptr)
                    *__bytes_read = got;
                return bigos::vfs::Status::Success;
            }
            // No in-order data. If the peer has closed (FIN consumed) and the buffer
            // is drained, that is EOF (read returns 0 with Success).
            if (bigos::net::tcp_peer_closed(s->tcb)) {
                if (__bytes_read != nullptr)
                    *__bytes_read = 0;
                return bigos::vfs::Status::Success;   // EOF
            }
            if (nonblocking)
                return bigos::vfs::Status::WouldBlock;
            // Bounded ordinary-context block: advance then wait on the connection
            // wait queue for data / EOF / reset, with a bounded timeout.
            stream_pump(s);
            if (!stream_tcb_alive(s))
                return bigos::vfs::Status::ConnectionReset;
            if (s->tcb->recv_count == 0 && !bigos::net::tcp_peer_closed(s->tcb)) {
                bigos::sched::WaitQueue *wq = bigos::net::tcp_wait_queue(s->tcb);
                if (wq != nullptr)
                    (void)bigos::sched::wait_queue_wait_until(wq, nullptr, nullptr, 2);
                else
                    bigos::sched::yield();
            }
        }
        return bigos::vfs::Status::WouldBlock;
    }

    bigos::vfs::Status stream_write_flags(
        bigos::vfs::File *__file, const void *__src, size_t __len, size_t *__bytes_written, bool __suppress_sigpipe) noexcept {
        constexpr uint32_t WRITE_MAX_ROUNDS = 16;
        if (__bytes_written != nullptr)
            *__bytes_written = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        bigos::net::StreamSocket *s = (bigos::net::StreamSocket *)__file->private_data;
        if (__len == 0)
            return bigos::vfs::Status::Success;
        if (__src == nullptr)
            return bigos::vfs::Status::InvalidArgument;
        if (!bigos::sched::can_block())
            return bigos::vfs::Status::WouldBlock;
        if (s->role != bigos::net::StreamSocket::Role::Connected || s->tcb == nullptr)
            return bigos::vfs::Status::NotConnected;

        const bool nonblocking = bigos::vfs::file_is_nonblocking(__file);
        const uint32_t rounds = nonblocking ? 1u : WRITE_MAX_ROUNDS;
        const uint8_t *in = (const uint8_t *)__src;
        size_t done = 0;
        for (uint32_t round = 0; round < rounds && done < __len; round++) {
            if (!stream_tcb_alive(s)) {
                // Connection reset. If bytes were already accepted, report them;
                // otherwise this is a broken-pipe write.
                if (done > 0)
                    break;
                return (bigos::vfs::Status)bigos::signal::raise_broken_pipe(
                    bigos::proc::current_process(), __suppress_sigpipe);
            }
            // A local FIN already sent means the write direction is closed: broken
            // pipe (BrokenPipe maps to -EPIPE) with SIGPIPE per the unified helper.
            if (bigos::net::tcp_write_closed(s->tcb)) {
                if (done > 0)
                    break;
                return (bigos::vfs::Status)bigos::signal::raise_broken_pipe(
                    bigos::proc::current_process(), __suppress_sigpipe);
            }
            uint32_t sent = 0;
            const bigos::net::Status st =
                bigos::net::tcp_send(s->context, s->tcb, in + done, (uint32_t)(__len - done), &sent);
            if (st != bigos::net::Status::Ok && st != bigos::net::Status::QueueFull &&
                st != bigos::net::Status::NotReady) {
                if (done > 0)
                    break;
                return bigos::vfs::Status::ConnectionReset;
            }
            done += sent;
            if (done >= __len)
                break;
            // Send path full (window / retransmit queue). Nonblocking returns what
            // was accepted (or EAGAIN if nothing yet); blocking advances and waits.
            if (nonblocking)
                break;
            stream_pump(s);
            if (!stream_tcb_alive(s)) {
                if (done > 0)
                    break;
                return (bigos::vfs::Status)bigos::signal::raise_broken_pipe(
                    bigos::proc::current_process(), __suppress_sigpipe);
            }
            bigos::sched::WaitQueue *wq = bigos::net::tcp_wait_queue(s->tcb);
            if (wq != nullptr)
                (void)bigos::sched::wait_queue_wait_until(wq, nullptr, nullptr, 2);
            else
                bigos::sched::yield();
        }
        if (done == 0 && nonblocking)
            return bigos::vfs::Status::WouldBlock;
        if (__bytes_written != nullptr)
            *__bytes_written = done;
        return bigos::vfs::Status::Success;
    }

    bigos::vfs::Status stream_write(
        bigos::vfs::File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept {
        // Ordinary write == send with flags==0 (SIGPIPE not suppressed).
        return stream_write_flags(__file, __src, __len, __bytes_written, false);
    }

    bigos::vfs::Status stream_lseek(bigos::vfs::File *, int64_t, int, uint64_t *) noexcept {
        return bigos::vfs::Status::NotSeekable;
    }

    // Level-triggered readiness snapshot shared with the would-block decisions in
    // stream_read/stream_write: readable == in-order data / EOF / (listener) a
    // completed connection is ready to accept; writable == Established with send
    // room; error == reset / unusable.
    uint32_t stream_poll(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::READY_ERROR;
        bigos::net::StreamSocket *s = (bigos::net::StreamSocket *)__file->private_data;
        switch (s->role) {
            case bigos::net::StreamSocket::Role::Listening: {
                if (s->tcb == nullptr || !stream_tcb_alive(s))
                    return bigos::vfs::READY_ERROR;
                return bigos::net::tcp_accept_ready(s->tcb) ? bigos::vfs::READY_READABLE : 0u;
            }
            case bigos::net::StreamSocket::Role::Connecting: {
                if (!stream_tcb_alive(s))
                    return bigos::vfs::READY_ERROR;
                // Connecting becomes writable once Established; readable/EOF too.
                if (bigos::net::tcp_is_established(s->tcb))
                    return bigos::vfs::READY_WRITABLE;
                return 0u;
            }
            case bigos::net::StreamSocket::Role::Connected: {
                if (!stream_tcb_alive(s))
                    return bigos::vfs::READY_ERROR;
                uint32_t ready = 0;
                if (s->tcb->recv_count > 0 || bigos::net::tcp_peer_closed(s->tcb))
                    ready |= bigos::vfs::READY_READABLE;
                if (bigos::net::tcp_is_established(s->tcb) && bigos::net::tcp_send_room(s->tcb))
                    ready |= bigos::vfs::READY_WRITABLE;
                return ready;
            }
            default:
                // Unbound / Bound / Closed: no connection to be ready on.
                return bigos::vfs::READY_ERROR;
        }
    }

    uint32_t stream_poll_wait(
        bigos::vfs::File *__file, uint32_t, bigos::sched::WaitQueue **__out, uint32_t __max) noexcept {
        if (__file == nullptr || __file->private_data == nullptr || __out == nullptr || __max == 0)
            return 0;
        bigos::net::StreamSocket *s = (bigos::net::StreamSocket *)__file->private_data;
        if (s->tcb == nullptr || !stream_tcb_alive(s))
            return 0;   // no queue -> poll_file already reports it ready/error
        bigos::sched::WaitQueue *wq = bigos::net::tcp_wait_queue(s->tcb);
        if (wq == nullptr)
            return 0;
        __out[0] = wq;
        return 1;
    }

    // Recycles the connection (if any) and frees the StreamSocket on the last
    // reference. vfs::release guarantees this runs exactly once on final close.
    void stream_close(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr || __file->private_data == nullptr)
            return;
        bigos::net::StreamSocket *s = (bigos::net::StreamSocket *)__file->private_data;
        if (s->tcb != nullptr && s->context != nullptr && stream_tcb_alive(s)) {
            // An Established connection gets an orderly FIN; anything else (LISTEN,
            // Connecting, half-open) is aborted/recycled.
            if (s->role == bigos::net::StreamSocket::Role::Connected && bigos::net::tcp_is_established(s->tcb))
                (void)bigos::net::tcp_close(s->context, s->tcb);
            else
                (void)bigos::net::tcp_abort(s->context, s->tcb);
        }
        bigos::free(s);
        __file->private_data = nullptr;
    }

    const bigos::vfs::FileOperations STREAM_SOCKET_OPS = {
        &stream_read, &stream_close, &stream_write, &stream_lseek, nullptr, nullptr, &stream_poll, &stream_poll_wait};
}   // namespace

NAMESPACE_BIGOS_BEG
namespace net {
    vfs::Status socket_create(Context *__context, vfs::File **__out_file) noexcept {
        if (__out_file == nullptr || __context == nullptr)
            return vfs::Status::InvalidArgument;
        *__out_file = nullptr;

        Socket *socket = (Socket *)bigos::kmalloc(sizeof(Socket));
        vfs::File *file = (vfs::File *)bigos::kmalloc(sizeof(vfs::File));
        if (socket == nullptr || file == nullptr) {
            if (socket != nullptr)
                bigos::free(socket);
            if (file != nullptr)
                bigos::free(file);
            return vfs::Status::NoMemory;
        }

        socket->context = __context;
        socket->endpoint = nullptr;
        socket->local_port = 0;
        socket->bound = false;

        file->ops = &SOCKET_OPS;
        file->vnode = nullptr;
        file->offset = 0;
        file->ref_count = 1;
        // A datagram socket is conceptually both readable and writable, but the
        // read/write ops deliberately return Unsupported; recvfrom/sendto are the
        // real data paths. readable is left true so the fd-table read guard does
        // not reject the fd before the socket-specific Unsupported is returned.
        file->readable = true;
        file->writable = true;
        file->close_on_exec = false;
        file->private_data = socket;
        file->identity = {};
        file->nonblocking = false;

        *__out_file = file;
        return vfs::Status::Success;
    }

    bool is_socket_file(const vfs::File *__file) noexcept {
        return __file != nullptr && __file->ops == &SOCKET_OPS;
    }

    Socket *socket_state(vfs::File *__file) noexcept {
        if (!is_socket_file(__file))
            return nullptr;
        return (Socket *)__file->private_data;
    }

    // Shared stream socket File initializer for a StreamSocket in a given role.
    static vfs::Status stream_socket_publish(Context *__context, TcpControlBlock *__tcb, uint16_t __local_port,
        StreamSocket::Role __role, vfs::File **__out_file) noexcept {
        if (__out_file == nullptr || __context == nullptr)
            return vfs::Status::InvalidArgument;
        *__out_file = nullptr;

        StreamSocket *ss = (StreamSocket *)bigos::kmalloc(sizeof(StreamSocket));
        vfs::File *file = (vfs::File *)bigos::kmalloc(sizeof(vfs::File));
        if (ss == nullptr || file == nullptr) {
            if (ss != nullptr)
                bigos::free(ss);
            if (file != nullptr)
                bigos::free(file);
            return vfs::Status::NoMemory;
        }

        ss->context = __context;
        ss->tcb = __tcb;
        ss->local_port = __local_port;
        ss->role = __role;
        ss->pending_error = 0;
        ss->tcb_generation = __tcb != nullptr ? tcp_slot_generation(__tcb) : 0;

        file->ops = &STREAM_SOCKET_OPS;
        file->vnode = nullptr;
        file->offset = 0;
        file->ref_count = 1;
        file->readable = true;
        file->writable = true;
        file->close_on_exec = false;
        file->private_data = ss;
        file->identity = {};
        file->nonblocking = false;

        *__out_file = file;
        return vfs::Status::Success;
    }

    vfs::Status stream_socket_create(Context *__context, vfs::File **__out_file) noexcept {
        return stream_socket_publish(__context, nullptr, 0, StreamSocket::Role::Unbound, __out_file);
    }

    vfs::Status stream_socket_create_accepted(
        Context *__context, TcpControlBlock *__tcb, uint16_t __local_port, vfs::File **__out_file) noexcept {
        if (__tcb == nullptr)
            return vfs::Status::InvalidArgument;
        return stream_socket_publish(__context, __tcb, __local_port, StreamSocket::Role::Connected, __out_file);
    }

    bool is_stream_socket_file(const vfs::File *__file) noexcept {
        return __file != nullptr && __file->ops == &STREAM_SOCKET_OPS;
    }

    StreamSocket *stream_socket_state(vfs::File *__file) noexcept {
        if (!is_stream_socket_file(__file))
            return nullptr;
        return (StreamSocket *)__file->private_data;
    }

    int64_t stream_socket_send(vfs::File *__file, const void *__buf, size_t __len, bool __suppress_sigpipe) noexcept {
        if (!is_stream_socket_file(__file))
            return -bigos::ENOTSOCK;
        size_t written = 0;
        const vfs::Status st = stream_write_flags(__file, __buf, __len, &written, __suppress_sigpipe);
        if (st == vfs::Status::Success)
            return (int64_t)written;
        // vfs::Status enumerators encode the negated errno the dispatcher writes
        // back (e.g. BrokenPipe == -32, WouldBlock == -11, NotConnected == -107).
        return (int64_t)st;
    }
}   // namespace net
NAMESPACE_BIGOS_END

#ifdef BIGOS_SOCKET_SMOKE
namespace {
    // Local frame-building helpers for the kernel-internal injection smoke. They
    // mirror the protocol layer's wire helpers but stay private to this smoke so
    // the closed loop runs without a real tap/network backend.
    void smoke_write_be16(uint8_t *__p, uint16_t __v) noexcept {
        __p[0] = (uint8_t)(__v >> 8);
        __p[1] = (uint8_t)__v;
    }

    void smoke_write_be32(uint8_t *__p, uint32_t __v) noexcept {
        __p[0] = (uint8_t)(__v >> 24);
        __p[1] = (uint8_t)(__v >> 16);
        __p[2] = (uint8_t)(__v >> 8);
        __p[3] = (uint8_t)__v;
    }

    uint16_t smoke_checksum(const uint8_t *__data, uint32_t __length) noexcept {
        uint32_t sum = 0;
        uint32_t i = 0;
        while (i + 1 < __length) {
            sum += (uint32_t)((__data[i] << 8) | __data[i + 1]);
            i += 2;
        }
        if (i < __length)
            sum += (uint32_t)__data[i] << 8;
        while ((sum >> 16) != 0)
            sum = (sum & 0xffffu) + (sum >> 16);
        return (uint16_t)~sum;
    }

    struct SmokeDevice {
        bigos::device::NetworkDevice net;
        bigos::net::MacAddress mac;
        uint32_t tx_count;
    };

    SmokeDevice g_smoke_dev = {};
    bigos::net::Context g_smoke_ctx = {};
    uint8_t g_smoke_frame[bigos::net::ETHERNET_MAX_FRAME_LEN] = {};

    bool smoke_ready(bigos::device::NetworkDevice *__d) noexcept { return __d != nullptr; }
    bool smoke_link_up(bigos::device::NetworkDevice *__d) noexcept { return __d != nullptr; }

    const uint8_t *smoke_mac(bigos::device::NetworkDevice *__d) noexcept {
        SmokeDevice *dev = __d == nullptr ? nullptr : (SmokeDevice *)__d->context;
        return dev == nullptr ? nullptr : dev->mac.bytes;
    }

    uint16_t smoke_mtu(bigos::device::NetworkDevice *) noexcept { return bigos::net::DEFAULT_MTU; }

    bigos::device::NetworkTxStatus smoke_transmit(
        bigos::device::NetworkDevice *__d, const void *__frame, uint32_t __len, uint64_t) noexcept {
        SmokeDevice *dev = __d == nullptr ? nullptr : (SmokeDevice *)__d->context;
        if (dev == nullptr || __frame == nullptr || __len > bigos::net::ETHERNET_MAX_FRAME_LEN)
            return bigos::device::NetworkTxStatus::InvalidFrame;
        dev->tx_count++;
        return bigos::device::NetworkTxStatus::Success;
    }

    bigos::device::NetworkRxStatus smoke_poll_rx(
        bigos::device::NetworkDevice *, bigos::device::NetworkRxFrame *) noexcept {
        return bigos::device::NetworkRxStatus::NoFrame;
    }

    bigos::device::NetworkRxStatus smoke_return_rx(
        bigos::device::NetworkDevice *, const bigos::device::NetworkRxFrame *) noexcept {
        return bigos::device::NetworkRxStatus::Success;
    }

    void smoke_diagnostics(bigos::device::NetworkDevice *, bigos::device::NetworkDiagnostics *) noexcept {}

    void smoke_fail(const char *__reason) noexcept {
        bigos::serial_puts("BIGOS_SOCKET_FAILED ");
        bigos::serial_puts(__reason);
        bigos::serial_puts("\n");
    }

    // Injects one UDP datagram from peer_ip:src_port to local:dst_port through the
    // protocol RX path, so a bound endpoint can receive it. Returns the inject
    // status.
    bigos::net::Status smoke_inject_udp(uint16_t __src_port, uint16_t __dst_port, const uint8_t *__payload,
                                        uint16_t __payload_len, bigos::net::Ipv4Address __peer,
                                        bigos::net::Ipv4Address __local, const bigos::net::MacAddress &__peer_mac,
                                        const bigos::net::MacAddress &__local_mac) noexcept {
        uint8_t *frame = g_smoke_frame;
        memcpy(frame, __local_mac.bytes, 6);
        memcpy(frame + 6, __peer_mac.bytes, 6);
        smoke_write_be16(frame + 12, bigos::net::ETHERNET_TYPE_IPV4);

        uint8_t *ip = frame + bigos::net::ETHERNET_HEADER_LEN;
        const uint16_t udp_len = (uint16_t)(8 + __payload_len);
        const uint16_t total_len = (uint16_t)(20 + udp_len);
        memset(ip, 0, total_len);
        ip[0] = (4 << 4) | 5;
        smoke_write_be16(ip + 2, total_len);
        ip[8] = 64;
        ip[9] = bigos::net::IPV4_PROTOCOL_UDP;
        smoke_write_be32(ip + 12, __peer.value);
        smoke_write_be32(ip + 16, __local.value);
        smoke_write_be16(ip + 10, smoke_checksum(ip, 20));
        uint8_t *udp = ip + 20;
        smoke_write_be16(udp + 0, __src_port);
        smoke_write_be16(udp + 2, __dst_port);
        smoke_write_be16(udp + 4, udp_len);
        smoke_write_be16(udp + 6, 0);   // zero checksum: protocol layer skips verification
        if (__payload_len != 0)
            memcpy(udp + 8, __payload, __payload_len);
        return bigos::net::inject_frame(&g_smoke_ctx, frame, bigos::net::ETHERNET_HEADER_LEN + total_len);
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace net {
    void socket_smoke_entry(void *) noexcept {
        g_smoke_dev = {};
        g_smoke_ctx = {};
        g_smoke_dev.mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
        g_smoke_dev.net.context = &g_smoke_dev;
        g_smoke_dev.net.ready = &smoke_ready;
        g_smoke_dev.net.link_up = &smoke_link_up;
        g_smoke_dev.net.mac = &smoke_mac;
        g_smoke_dev.net.mtu = &smoke_mtu;
        g_smoke_dev.net.transmit = &smoke_transmit;
        g_smoke_dev.net.poll_rx = &smoke_poll_rx;
        g_smoke_dev.net.return_rx = &smoke_return_rx;
        g_smoke_dev.net.diagnostics = &smoke_diagnostics;

        Config config = {};
        config.local_ipv4 = make_ipv4(10, 0, 2, 15);
        config.netmask = make_ipv4(255, 255, 255, 0);
        config.direct_peer_ipv4 = make_ipv4(10, 0, 2, 2);
        config.local_mac = g_smoke_dev.mac;
        config.mtu = DEFAULT_MTU;
        config.has_direct_peer = true;
        if (init(&g_smoke_ctx, &g_smoke_dev.net, &config) != Status::Ok) {
            smoke_fail("init");
            return;
        }

        // 1) Socket object layer: create an unbound socket File, verify ops
        //    identity and the unsupported read/write semantics, and confirm an
        //    unbound socket cannot bind a zero/illegal port.
        vfs::File *file = nullptr;
        if (socket_create(&g_smoke_ctx, &file) != vfs::Status::Success || file == nullptr) {
            smoke_fail("create");
            return;
        }
        if (!is_socket_file(file) || socket_state(file) == nullptr || file->ref_count != 1) {
            smoke_fail("identity");
            vfs::release(file);
            return;
        }
        size_t io = 0;
        uint8_t scratch[4] = {0};
        if (vfs::read(file, scratch, sizeof(scratch), &io) != vfs::Status::Unsupported || io != 0) {
            smoke_fail("read-unsupported");
            vfs::release(file);
            return;
        }
        if (vfs::write(file, scratch, sizeof(scratch), &io) != vfs::Status::Unsupported || io != 0) {
            smoke_fail("write-unsupported");
            vfs::release(file);
            return;
        }

        // 2) Receive before bind has no local port: the protocol layer returns
        //    NotBound for an inactive endpoint.
        Socket *socket = socket_state(file);
        UdpDatagram early = {};
        if (socket->endpoint != nullptr || udp_receive_from(socket->endpoint, &early) == Status::Ok) {
            smoke_fail("recv-before-bind");
            vfs::release(file);
            return;
        }

        // 3) Bind a local port through the same API the SYS_BIND path uses.
        const uint16_t local_port = 4000;
        UdpEndpoint *endpoint = nullptr;
        if (udp_bind(&g_smoke_ctx, local_port, &endpoint) != Status::Ok || endpoint == nullptr) {
            smoke_fail("bind");
            vfs::release(file);
            return;
        }
        socket->endpoint = endpoint;
        socket->local_port = local_port;
        socket->bound = true;

        // Duplicate bind on the same port is rejected deterministically.
        UdpEndpoint *dup = nullptr;
        if (udp_bind(&g_smoke_ctx, local_port, &dup) != Status::AlreadyBound) {
            smoke_fail("dup-bind");
            vfs::release(file);
            return;
        }

        // Resolve the peer first so udp_send_to does not stall on ARP.
        const MacAddress peer_mac = {{0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc}};
        const Ipv4Address peer_ip = make_ipv4(10, 0, 2, 2);
        const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        uint8_t *frame = g_smoke_frame;
        memcpy(frame, broadcast_mac, 6);
        memcpy(frame + 6, peer_mac.bytes, 6);
        smoke_write_be16(frame + 12, ETHERNET_TYPE_ARP);
        uint8_t *arp = frame + ETHERNET_HEADER_LEN;
        smoke_write_be16(arp + 0, 1);
        smoke_write_be16(arp + 2, ETHERNET_TYPE_IPV4);
        arp[4] = 6;
        arp[5] = 4;
        smoke_write_be16(arp + 6, 1);   // request
        memcpy(arp + 8, peer_mac.bytes, 6);
        smoke_write_be32(arp + 14, peer_ip.value);
        smoke_write_be32(arp + 24, config.local_ipv4.value);
        if (inject_frame(&g_smoke_ctx, frame, ETHERNET_HEADER_LEN + ARP_PACKET_LEN) != Status::Ok) {
            smoke_fail("arp");
            vfs::release(file);
            return;
        }

        // 4) sendto closed loop: send to the now-resolved peer.
        const uint8_t msg[] = {'o', 'k'};
        if (udp_send_to(&g_smoke_ctx, endpoint, peer_ip, 5000, msg, sizeof(msg), 0) != Status::Ok) {
            smoke_fail("send");
            vfs::release(file);
            return;
        }

        // 5) recvfrom closed loop: inject a datagram, advance, and receive it
        //    with the correct source address and payload.
        if (smoke_inject_udp(5000, local_port, msg, sizeof(msg), peer_ip, config.local_ipv4, peer_mac,
                             config.local_mac) != Status::Ok) {
            smoke_fail("inject");
            vfs::release(file);
            return;
        }
        UdpDatagram got = {};
        if (udp_receive_from(endpoint, &got) != Status::Ok || got.payload_length != sizeof(msg) ||
            got.payload[0] != 'o' || got.payload[1] != 'k' || got.source_port != 5000 ||
            got.source_ipv4.value != peer_ip.value) {
            smoke_fail("recv");
            vfs::release(file);
            return;
        }

        // 6) No-data path: a second receive with no injected datagram returns
        //    NoData (which the syscall maps to -EAGAIN), not a stall.
        UdpDatagram none = {};
        if (udp_receive_from(endpoint, &none) != Status::NoData) {
            smoke_fail("no-data");
            vfs::release(file);
            return;
        }

        // 7) Oversized payload is rejected deterministically by the protocol layer.
        static uint8_t big[UDP_MAX_PAYLOAD + 1] = {};
        if (udp_send_to(&g_smoke_ctx, endpoint, peer_ip, 5000, big, (uint16_t)sizeof(big), 0) != Status::TooLarge) {
            smoke_fail("too-large");
            vfs::release(file);
            return;
        }

        // 8) Lifecycle: releasing the last reference runs SOCKET_OPS.close, which
        //    recycles the endpoint exactly once. The slot is then free to rebind.
        vfs::release(file);
        UdpEndpoint *rebind = nullptr;
        if (udp_bind(&g_smoke_ctx, local_port, &rebind) != Status::Ok || rebind == nullptr) {
            smoke_fail("rebind-after-close");
            return;
        }
        (void)udp_close(&g_smoke_ctx, rebind);

        bigos::serial_puts("BIGOS_SOCKET_PASSED\n");
    }
}   // namespace net
NAMESPACE_BIGOS_END
#endif   // BIGOS_SOCKET_SMOKE

#ifdef BIGOS_STREAM_SOCKET_SMOKE
namespace {
    // Kernel-internal / backend-level bounded stream socket closed-loop smoke. It
    // runs from a blockable kernel thread over a LoopbackReady context (no
    // frame-level device), driving the STREAM_SOCKET_OPS read/write/poll paths and
    // the kernel-internal tcp_* API the SYS_CONNECT/LISTEN/ACCEPT/SEND syscalls
    // use, without a user process. Syscall-only argument validation (SockAddrIn
    // copy, sys_send flags -EINVAL, sendto/recvfrom -EOPNOTSUPP) is covered by the
    // syscall layer and source review; this smoke covers the backend/protocol
    // closed loop deterministically.
    bigos::net::Context g_stream_ctx = {};

    void stream_smoke_fail(const char *__reason) noexcept {
        bigos::serial_puts("BIGOS_STREAM_SOCKET_FAILED ");
        bigos::serial_puts(__reason);
        bigos::serial_puts("\n");
    }

    bool stream_bytes_equal(const uint8_t *__a, const uint8_t *__b, uint32_t __n) noexcept {
        for (uint32_t i = 0; i < __n; i++) {
            if (__a[i] != __b[i])
                return false;
        }
        return true;
    }

    bool run_stream_socket_smoke() noexcept {
        using namespace bigos;
        using namespace bigos::net;
        tcp_reset_state();
        g_stream_ctx = {};

        Config config = {};
        config.local_ipv4 = make_ipv4(127, 0, 0, 1);
        config.netmask = make_ipv4(255, 0, 0, 0);
        config.mtu = DEFAULT_MTU;
        if (init(&g_stream_ctx, nullptr, &config) != Status::Ok || state(&g_stream_ctx) != State::LoopbackReady) {
            stream_smoke_fail("init");
            return false;
        }
        const Ipv4Address lip = config.local_ipv4;
        const uint16_t listen_port = 7000;

        // 1) Create a stream socket File; identity + unconnected read/write reject.
        vfs::File *lf = nullptr;
        if (stream_socket_create(&g_stream_ctx, &lf) != vfs::Status::Success || lf == nullptr) {
            stream_smoke_fail("create");
            return false;
        }
        if (!is_stream_socket_file(lf) || stream_socket_state(lf) == nullptr) {
            stream_smoke_fail("identity");
            vfs::release(lf);
            return false;
        }
        size_t io = 0;
        uint8_t scratch[8] = {0};
        if (vfs::read(lf, scratch, 4, &io) != vfs::Status::NotConnected ||
            vfs::write(lf, scratch, 4, &io) != vfs::Status::NotConnected) {
            stream_smoke_fail("unconnected");
            vfs::release(lf);
            return false;
        }

        // 2) Listen: listener stays LISTEN, not yet readable (empty accept queue).
        StreamSocket *ls = stream_socket_state(lf);
        ls->local_port = listen_port;
        ls->role = StreamSocket::Role::Bound;
        TcpControlBlock *ltcb = nullptr;
        if (tcp_listen(&g_stream_ctx, lip, listen_port, &ltcb) != Status::Ok || ltcb == nullptr) {
            stream_smoke_fail("listen");
            vfs::release(lf);
            return false;
        }
        ls->tcb = ltcb;
        ls->tcb_generation = tcp_slot_generation(ltcb);
        ls->role = StreamSocket::Role::Listening;
        if ((vfs::poll_file(lf) & vfs::READY_READABLE) != 0) {
            stream_smoke_fail("listener-premature-readable");
            vfs::release(lf);
            return false;
        }

        // 3) Client active-open completes synchronously under loopback.
        vfs::File *cf = nullptr;
        if (stream_socket_create(&g_stream_ctx, &cf) != vfs::Status::Success || cf == nullptr) {
            stream_smoke_fail("client-create");
            vfs::release(lf);
            return false;
        }
        StreamSocket *cs = stream_socket_state(cf);
        TcpControlBlock *ctcb = nullptr;
        if (tcp_open(&g_stream_ctx, lip, 40000, lip, listen_port, &ctcb) != Status::Ok || ctcb == nullptr ||
            !tcp_is_established(ctcb)) {
            stream_smoke_fail("connect");
            vfs::release(cf);
            vfs::release(lf);
            return false;
        }
        cs->tcb = ctcb;
        cs->tcb_generation = tcp_slot_generation(ctcb);
        cs->role = StreamSocket::Role::Connected;

        // 4) Listener now reports readable (a completed connection can be accepted).
        if ((vfs::poll_file(lf) & vfs::READY_READABLE) == 0) {
            stream_smoke_fail("listener-accept-ready");
            vfs::release(cf);
            vfs::release(lf);
            return false;
        }

        // 5) Accept the completed child and publish it under a new stream socket.
        TcpControlBlock *child = nullptr;
        if (tcp_accept(&g_stream_ctx, ltcb, &child) != Status::Ok || child == nullptr) {
            stream_smoke_fail("accept");
            vfs::release(cf);
            vfs::release(lf);
            return false;
        }
        vfs::File *af = nullptr;
        if (stream_socket_create_accepted(&g_stream_ctx, child, listen_port, &af) != vfs::Status::Success ||
            af == nullptr) {
            stream_smoke_fail("accept-publish");
            (void)tcp_abort(&g_stream_ctx, child);
            vfs::release(cf);
            vfs::release(lf);
            return false;
        }

        // 6) Ordered bidirectional data: client -> accepted, accepted -> client.
        const uint8_t ping[] = {'p', 'i', 'n', 'g'};
        io = 0;
        if (vfs::write(cf, ping, sizeof(ping), &io) != vfs::Status::Success || io != sizeof(ping)) {
            stream_smoke_fail("write-c2s");
            goto fail_all;
        }
        {
            uint8_t rx[16] = {};
            io = 0;
            if (vfs::read(af, rx, sizeof(rx), &io) != vfs::Status::Success || io != sizeof(ping) ||
                !stream_bytes_equal(rx, ping, sizeof(ping))) {
                stream_smoke_fail("read-c2s");
                goto fail_all;
            }
            const uint8_t pong[] = {'p', 'o', 'n', 'g'};
            io = 0;
            if (vfs::write(af, pong, sizeof(pong), &io) != vfs::Status::Success || io != sizeof(pong)) {
                stream_smoke_fail("write-s2c");
                goto fail_all;
            }
            io = 0;
            if (vfs::read(cf, rx, sizeof(rx), &io) != vfs::Status::Success || io != sizeof(pong) ||
                !stream_bytes_equal(rx, pong, sizeof(pong))) {
                stream_smoke_fail("read-s2c");
                goto fail_all;
            }
        }

        // 7) An Established connection reports writable.
        if ((vfs::poll_file(cf) & vfs::READY_WRITABLE) == 0) {
            stream_smoke_fail("poll-writable");
            goto fail_all;
        }

        // 8) Nonblocking read with no pending data returns would-block (-EAGAIN).
        cf->nonblocking = true;
        {
            uint8_t rx[4] = {};
            io = 0;
            if (vfs::read(cf, rx, sizeof(rx), &io) != vfs::Status::WouldBlock) {
                stream_smoke_fail("nonblock-eagain");
                cf->nonblocking = false;
                goto fail_all;
            }
        }
        cf->nonblocking = false;

        // 9) EOF: the accepted peer closes; the client reads 0 after draining.
        if (tcp_close(&g_stream_ctx, child) != Status::Ok) {
            stream_smoke_fail("peer-close");
            goto fail_all;
        }
        {
            uint8_t rx[16] = {};
            io = 0;
            if (vfs::read(cf, rx, sizeof(rx), &io) != vfs::Status::Success || io != 0) {
                stream_smoke_fail("eof");
                goto fail_all;
            }
        }

        // 10) Broken pipe: close the client's own write direction, then a write
        // returns -EPIPE (BrokenPipe); a MSG_NOSIGNAL send also returns -EPIPE.
        (void)tcp_close(&g_stream_ctx, ctcb);
        {
            io = 0;
            const vfs::Status ws = vfs::write(cf, ping, sizeof(ping), &io);
            if (ws != vfs::Status::BrokenPipe) {
                stream_smoke_fail("epipe");
                goto fail_all;
            }
            if (stream_socket_send(cf, ping, sizeof(ping), true) != -bigos::EPIPE) {
                stream_smoke_fail("msg-nosignal-epipe");
                goto fail_all;
            }
        }

        // Release the first connection set. stream_close aborts/closes remaining
        // TCBs; the listener is aborted.
        vfs::release(af);
        af = nullptr;
        vfs::release(cf);
        cf = nullptr;
        vfs::release(lf);
        lf = nullptr;

        // 11) Connection reset detection: build a fresh connection, recycle the
        // client's TCB out from under it, then a read reports connection reset.
        tcp_reset_state();
        {
            vfs::File *rf = nullptr;
            if (stream_socket_create(&g_stream_ctx, &rf) != vfs::Status::Success || rf == nullptr) {
                stream_smoke_fail("reset-create");
                return false;
            }
            StreamSocket *rs = stream_socket_state(rf);
            TcpControlBlock *rtcb = nullptr;
            if (tcp_listen(&g_stream_ctx, lip, listen_port, &rtcb) != Status::Ok) {
                stream_smoke_fail("reset-listen");
                vfs::release(rf);
                return false;
            }
            TcpControlBlock *rc = nullptr;
            if (tcp_open(&g_stream_ctx, lip, 40001, lip, listen_port, &rc) != Status::Ok || rc == nullptr ||
                !tcp_is_established(rc)) {
                stream_smoke_fail("reset-connect");
                vfs::release(rf);
                return false;
            }
            rs->tcb = rc;
            rs->tcb_generation = tcp_slot_generation(rc);
            rs->role = StreamSocket::Role::Connected;
            // Recycle the client's connection slot; the socket must observe reset.
            (void)tcp_abort(&g_stream_ctx, rc);
            uint8_t rx[4] = {};
            io = 0;
            if (vfs::read(rf, rx, sizeof(rx), &io) != vfs::Status::ConnectionReset) {
                stream_smoke_fail("reset-read");
                vfs::release(rf);
                return false;
            }
            vfs::release(rf);
        }

        tcp_reset_state();
        return true;

    fail_all:
        if (af != nullptr)
            vfs::release(af);
        if (cf != nullptr)
            vfs::release(cf);
        if (lf != nullptr)
            vfs::release(lf);
        tcp_reset_state();
        return false;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace net {
    void stream_socket_smoke_entry(void *) noexcept {
        if (run_stream_socket_smoke())
            bigos::serial_puts("BIGOS_STREAM_SOCKET_PASSED\n");
    }
}   // namespace net
NAMESPACE_BIGOS_END
#endif   // BIGOS_STREAM_SOCKET_SMOKE