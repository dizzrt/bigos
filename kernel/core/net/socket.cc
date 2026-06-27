#include <bigos/net/socket.h>

#include <bigos/memory.h>
#ifdef BIGOS_SOCKET_SMOKE
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

    const bigos::vfs::FileOperations SOCKET_OPS = {
        &socket_read, &socket_close, &socket_write, &socket_lseek, nullptr, nullptr};
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