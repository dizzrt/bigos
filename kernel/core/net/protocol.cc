#include <bigos/net.h>

#include <bigos/io.h>
#include <bigos/sched.h>

#include <string.h>

namespace {
    constexpr uint16_t ARP_HW_ETHERNET = 1;
    constexpr uint16_t ARP_OP_REQUEST = 1;
    constexpr uint16_t ARP_OP_REPLY = 2;
    constexpr uint8_t IPV4_VERSION = 4;
    constexpr uint8_t IPV4_MIN_IHL = 5;
    constexpr uint8_t IPV4_TTL = 64;
    constexpr uint8_t ICMP_ECHO_REPLY = 0;
    constexpr uint8_t ICMP_ECHO_REQUEST = 8;
    constexpr uint32_t BROADCAST_IPV4 = 0xffffffffu;

    const bigos::net::MacAddress BROADCAST_MAC = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    bigos::net::Context g_default_context = {};

    bool ordinary_context() noexcept {
        const bigos::cpu::LocalState &local = bigos::cpu::current_state();
        return local.irq_nesting_depth == 0 && local.nonblocking_depth == 0;
    }

    uint16_t read_be16(const uint8_t *__p) noexcept {
        return (uint16_t)(((uint16_t)__p[0] << 8) | __p[1]);
    }

    uint32_t read_be32(const uint8_t *__p) noexcept {
        return ((uint32_t)__p[0] << 24) | ((uint32_t)__p[1] << 16) | ((uint32_t)__p[2] << 8) | __p[3];
    }

    void write_be16(uint8_t *__p, uint16_t __v) noexcept {
        __p[0] = (uint8_t)(__v >> 8);
        __p[1] = (uint8_t)__v;
    }

    void write_be32(uint8_t *__p, uint32_t __v) noexcept {
        __p[0] = (uint8_t)(__v >> 24);
        __p[1] = (uint8_t)(__v >> 16);
        __p[2] = (uint8_t)(__v >> 8);
        __p[3] = (uint8_t)__v;
    }

    bool ipv4_equal(bigos::net::Ipv4Address __a, bigos::net::Ipv4Address __b) noexcept {
        return __a.value == __b.value;
    }

    bool ipv4_zero(bigos::net::Ipv4Address __ip) noexcept {
        return __ip.value == 0;
    }

    bool mac_zero(const bigos::net::MacAddress &__mac) noexcept {
        for (uint32_t i = 0; i < sizeof(__mac.bytes); i++) {
            if (__mac.bytes[i] != 0)
                return false;
        }
        return true;
    }

    bool accepted_destination(const bigos::net::Context *__ctx, const uint8_t *__mac) noexcept {
        bigos::net::MacAddress dst = {};
        memcpy(dst.bytes, __mac, sizeof(dst.bytes));
        return bigos::net::mac_equal(dst, __ctx->config.local_mac) || bigos::net::mac_equal(dst, BROADCAST_MAC);
    }

    uint16_t internet_checksum(const uint8_t *__data, uint32_t __length, uint32_t __sum = 0) noexcept {
        uint32_t i = 0;
        while (i + 1 < __length) {
            __sum += read_be16(__data + i);
            i += 2;
        }
        if (i < __length)
            __sum += (uint16_t)__data[i] << 8;
        while ((__sum >> 16) != 0)
            __sum = (__sum & 0xffffu) + (__sum >> 16);
        return (uint16_t)~__sum;
    }

    void set_status(bigos::net::Context *__ctx, bigos::net::Status __status) noexcept {
        if (__ctx != nullptr)
            __ctx->diagnostics.last_status = __status;
    }

    bool validate_ready(bigos::net::Context *__ctx) noexcept {
        if (__ctx == nullptr) {
            return false;
        }
        if (!ordinary_context()) {
            __ctx->diagnostics.irq_context_rejected++;
            set_status(__ctx, bigos::net::Status::UnsupportedContext);
            return false;
        }
        if (__ctx->state != bigos::net::State::Ready || __ctx->device == nullptr) {
            set_status(__ctx, bigos::net::Status::Disabled);
            return false;
        }
        return true;
    }

    bigos::net::ArpEntry *find_arp(bigos::net::Context *__ctx, bigos::net::Ipv4Address __ipv4) noexcept {
        for (uint32_t i = 0; i < bigos::net::ARP_CACHE_CAPACITY; i++) {
            if (__ctx->arp_cache[i].valid && ipv4_equal(__ctx->arp_cache[i].ipv4, __ipv4))
                return &__ctx->arp_cache[i];
        }
        return nullptr;
    }

    bigos::net::Status update_arp(bigos::net::Context *__ctx, bigos::net::Ipv4Address __ipv4,
                                  const bigos::net::MacAddress &__mac) noexcept {
        bigos::net::ArpEntry *entry = find_arp(__ctx, __ipv4);
        if (entry == nullptr) {
            for (uint32_t i = 0; i < bigos::net::ARP_CACHE_CAPACITY; i++) {
                if (!__ctx->arp_cache[i].valid) {
                    entry = &__ctx->arp_cache[i];
                    break;
                }
            }
        }
        if (entry == nullptr) {
            __ctx->diagnostics.arp_cache_full++;
            return bigos::net::Status::CacheFull;
        }
        entry->valid = true;
        entry->ipv4 = __ipv4;
        entry->mac = __mac;
        entry->updated_tick = bigos::timer::ticks();
        return bigos::net::Status::Ok;
    }

    bigos::net::Status transmit_ethernet(bigos::net::Context *__ctx, const bigos::net::MacAddress &__dst,
                                         uint16_t __ethertype, const uint8_t *__payload,
                                         uint16_t __payload_length) noexcept {
        const uint32_t frame_len = bigos::net::ETHERNET_HEADER_LEN + __payload_length;
        const uint16_t mtu = __ctx->config.mtu == 0 ? bigos::net::DEFAULT_MTU : __ctx->config.mtu;
        if (__payload == nullptr || __payload_length > mtu || frame_len > bigos::net::ETHERNET_MAX_FRAME_LEN) {
            set_status(__ctx, bigos::net::Status::TooLarge);
            return bigos::net::Status::TooLarge;
        }
        memcpy(__ctx->tx_frame, __dst.bytes, sizeof(__dst.bytes));
        memcpy(__ctx->tx_frame + 6, __ctx->config.local_mac.bytes, sizeof(__ctx->config.local_mac.bytes));
        write_be16(__ctx->tx_frame + 12, __ethertype);
        memcpy(__ctx->tx_frame + bigos::net::ETHERNET_HEADER_LEN, __payload, __payload_length);
        const bigos::device::NetworkTxStatus tx =
            __ctx->device->transmit(__ctx->device, __ctx->tx_frame, frame_len, 30);
        if (tx != bigos::device::NetworkTxStatus::Success) {
            __ctx->diagnostics.tx_failed++;
            set_status(__ctx, bigos::net::Status::DeviceTxFailure);
            return bigos::net::Status::DeviceTxFailure;
        }
        __ctx->diagnostics.tx_frames++;
        set_status(__ctx, bigos::net::Status::Ok);
        return bigos::net::Status::Ok;
    }

    bigos::net::Status send_arp(bigos::net::Context *__ctx, uint16_t __op, const bigos::net::MacAddress &__dst_mac,
                                bigos::net::Ipv4Address __target_ipv4,
                                const bigos::net::MacAddress &__target_mac) noexcept {
        uint8_t payload[bigos::net::ARP_PACKET_LEN] = {};
        write_be16(payload + 0, ARP_HW_ETHERNET);
        write_be16(payload + 2, bigos::net::ETHERNET_TYPE_IPV4);
        payload[4] = 6;
        payload[5] = 4;
        write_be16(payload + 6, __op);
        memcpy(payload + 8, __ctx->config.local_mac.bytes, 6);
        write_be32(payload + 14, __ctx->config.local_ipv4.value);
        memcpy(payload + 18, __target_mac.bytes, 6);
        write_be32(payload + 24, __target_ipv4.value);
        const bigos::net::Status status =
            transmit_ethernet(__ctx, __dst_mac, bigos::net::ETHERNET_TYPE_ARP, payload, sizeof(payload));
        if (status == bigos::net::Status::Ok) {
            if (__op == ARP_OP_REQUEST)
                __ctx->diagnostics.arp_requests++;
            else
                __ctx->diagnostics.arp_replies++;
        }
        return status;
    }

    bigos::net::Ipv4Address route_destination(const bigos::net::Context *__ctx,
                                              bigos::net::Ipv4Address __destination) noexcept {
        if (__ctx->config.has_direct_peer)
            return __ctx->config.direct_peer_ipv4;
        if (__ctx->config.has_gateway) {
            const uint32_t local_net = __ctx->config.local_ipv4.value & __ctx->config.netmask.value;
            const uint32_t dest_net = __destination.value & __ctx->config.netmask.value;
            if (local_net != dest_net)
                return __ctx->config.gateway_ipv4;
        }
        return __destination;
    }

    bigos::net::Status send_ipv4(bigos::net::Context *__ctx, bigos::net::Ipv4Address __destination,
                                 uint8_t __protocol, const uint8_t *__payload,
                                 uint16_t __payload_length) noexcept {
        if (__payload == nullptr)
            return bigos::net::Status::InvalidArgument;
        const uint16_t total_len = (uint16_t)(20 + __payload_length);
        if (total_len > __ctx->config.mtu) {
            set_status(__ctx, bigos::net::Status::TooLarge);
            return bigos::net::Status::TooLarge;
        }

        bigos::net::MacAddress dst_mac = {};
        const bigos::net::Status arp =
            bigos::net::arp_resolve(__ctx, route_destination(__ctx, __destination), &dst_mac, 0);
        if (arp != bigos::net::Status::Ok)
            return arp;

        uint8_t packet[20 + bigos::net::UDP_MAX_PAYLOAD + 8] = {};
        packet[0] = (IPV4_VERSION << 4) | IPV4_MIN_IHL;
        packet[1] = 0;
        write_be16(packet + 2, total_len);
        write_be16(packet + 4, ++__ctx->ipv4_identification);
        write_be16(packet + 6, 0);
        packet[8] = IPV4_TTL;
        packet[9] = __protocol;
        write_be32(packet + 12, __ctx->config.local_ipv4.value);
        write_be32(packet + 16, __destination.value);
        write_be16(packet + 10, internet_checksum(packet, 20));
        memcpy(packet + 20, __payload, __payload_length);
        return transmit_ethernet(__ctx, dst_mac, bigos::net::ETHERNET_TYPE_IPV4, packet, total_len);
    }

    bigos::net::Status handle_icmp(bigos::net::Context *__ctx, bigos::net::Ipv4Address __source,
                                   const uint8_t *__payload, uint16_t __length) noexcept {
        if (__payload == nullptr || __length < 8 || internet_checksum(__payload, __length) != 0) {
            __ctx->diagnostics.icmp_malformed++;
            set_status(__ctx, bigos::net::Status::Malformed);
            return bigos::net::Status::Malformed;
        }
        if (__payload[0] != ICMP_ECHO_REQUEST || __payload[1] != 0) {
            __ctx->diagnostics.icmp_malformed++;
            set_status(__ctx, bigos::net::Status::Unsupported);
            return bigos::net::Status::Unsupported;
        }
        uint8_t reply[8 + bigos::net::UDP_MAX_PAYLOAD] = {};
        if (__length > sizeof(reply)) {
            __ctx->diagnostics.icmp_malformed++;
            set_status(__ctx, bigos::net::Status::TooLarge);
            return bigos::net::Status::TooLarge;
        }
        memcpy(reply, __payload, __length);
        reply[0] = ICMP_ECHO_REPLY;
        reply[2] = 0;
        reply[3] = 0;
        write_be16(reply + 2, internet_checksum(reply, __length));
        __ctx->diagnostics.icmp_echo_requests++;
        const bigos::net::Status status = send_ipv4(__ctx, __source, bigos::net::IPV4_PROTOCOL_ICMP, reply, __length);
        if (status == bigos::net::Status::Ok)
            __ctx->diagnostics.icmp_echo_replies++;
        return status;
    }

    bigos::net::UdpEndpoint *find_udp_endpoint(bigos::net::Context *__ctx, uint16_t __port) noexcept {
        for (uint32_t i = 0; i < bigos::net::UDP_ENDPOINT_CAPACITY; i++) {
            bigos::net::UdpEndpoint &endpoint = __ctx->udp_endpoints[i];
            if (endpoint.active && endpoint.local_port == __port)
                return &endpoint;
        }
        return nullptr;
    }

    bigos::net::Status handle_udp(bigos::net::Context *__ctx, bigos::net::Ipv4Address __source,
                                  const uint8_t *__payload, uint16_t __length) noexcept {
        if (__payload == nullptr || __length < 8) {
            __ctx->diagnostics.udp_malformed++;
            return bigos::net::Status::Malformed;
        }
        const uint16_t source_port = read_be16(__payload + 0);
        const uint16_t dest_port = read_be16(__payload + 2);
        const uint16_t udp_len = read_be16(__payload + 4);
        const uint16_t checksum = read_be16(__payload + 6);
        if (udp_len < 8 || udp_len > __length) {
            __ctx->diagnostics.udp_malformed++;
            return bigos::net::Status::Malformed;
        }
        if (checksum != 0) {
            uint8_t pseudo[12 + 8 + bigos::net::UDP_MAX_PAYLOAD] = {};
            if (udp_len > 8 + bigos::net::UDP_MAX_PAYLOAD) {
                __ctx->diagnostics.udp_payload_overflow++;
                return bigos::net::Status::TooLarge;
            }
            write_be32(pseudo + 0, __source.value);
            write_be32(pseudo + 4, __ctx->config.local_ipv4.value);
            pseudo[8] = 0;
            pseudo[9] = bigos::net::IPV4_PROTOCOL_UDP;
            write_be16(pseudo + 10, udp_len);
            memcpy(pseudo + 12, __payload, udp_len);
            if (internet_checksum(pseudo, 12 + udp_len) != 0) {
                __ctx->diagnostics.udp_malformed++;
                return bigos::net::Status::Malformed;
            }
        }

        bigos::net::UdpEndpoint *endpoint = find_udp_endpoint(__ctx, dest_port);
        if (endpoint == nullptr) {
            __ctx->diagnostics.udp_unbound++;
            return bigos::net::Status::NotBound;
        }
        const uint16_t payload_len = (uint16_t)(udp_len - 8);
        if (payload_len > bigos::net::UDP_MAX_PAYLOAD) {
            __ctx->diagnostics.udp_payload_overflow++;
            return bigos::net::Status::TooLarge;
        }
        if (endpoint->rx_count >= bigos::net::UDP_RX_QUEUE_CAPACITY) {
            __ctx->diagnostics.udp_queue_full++;
            return bigos::net::Status::QueueFull;
        }
        const uint32_t index = (endpoint->rx_head + endpoint->rx_count) % bigos::net::UDP_RX_QUEUE_CAPACITY;
        bigos::net::UdpDatagram &datagram = endpoint->rx_queue[index];
        datagram.source_ipv4 = __source;
        datagram.source_port = source_port;
        datagram.payload_length = payload_len;
        memcpy(datagram.payload, __payload + 8, payload_len);
        endpoint->rx_count++;
        __ctx->diagnostics.udp_received++;
        return bigos::net::Status::Ok;
    }

    bigos::net::Status handle_ipv4(bigos::net::Context *__ctx, const uint8_t *__payload, uint16_t __length) noexcept {
        if (__payload == nullptr || __length < 20) {
            __ctx->diagnostics.ipv4_malformed++;
            return bigos::net::Status::Malformed;
        }
        const uint8_t version = __payload[0] >> 4;
        const uint8_t ihl = __payload[0] & 0x0f;
        if (version != IPV4_VERSION || ihl < IPV4_MIN_IHL) {
            __ctx->diagnostics.ipv4_malformed++;
            return bigos::net::Status::Malformed;
        }
        const uint16_t header_len = (uint16_t)ihl * 4;
        const uint16_t total_len = read_be16(__payload + 2);
        if (total_len < header_len || total_len > __length) {
            __ctx->diagnostics.ipv4_malformed++;
            return bigos::net::Status::Malformed;
        }
        if (internet_checksum(__payload, header_len) != 0) {
            __ctx->diagnostics.ipv4_bad_checksum++;
            return bigos::net::Status::Malformed;
        }
        const uint16_t frag = read_be16(__payload + 6);
        if ((frag & 0x3fffu) != 0 || (frag & 0x2000u) != 0) {
            __ctx->diagnostics.ipv4_fragmented++;
            return bigos::net::Status::Unsupported;
        }
        const bigos::net::Ipv4Address source = {read_be32(__payload + 12)};
        const bigos::net::Ipv4Address dest = {read_be32(__payload + 16)};
        if (!ipv4_equal(dest, __ctx->config.local_ipv4) && dest.value != BROADCAST_IPV4) {
            __ctx->diagnostics.ipv4_not_local++;
            return bigos::net::Status::NotLocal;
        }
        __ctx->diagnostics.ipv4_rx++;
        const uint8_t protocol = __payload[9];
        const uint8_t *body = __payload + header_len;
        const uint16_t body_len = (uint16_t)(total_len - header_len);
        if (protocol == bigos::net::IPV4_PROTOCOL_ICMP)
            return handle_icmp(__ctx, source, body, body_len);
        if (protocol == bigos::net::IPV4_PROTOCOL_UDP)
            return handle_udp(__ctx, source, body, body_len);
        __ctx->diagnostics.ipv4_unsupported_protocol++;
        return bigos::net::Status::Unsupported;
    }

    bigos::net::Status handle_arp(bigos::net::Context *__ctx, const uint8_t *__payload, uint16_t __length) noexcept {
        if (__payload == nullptr || __length < bigos::net::ARP_PACKET_LEN ||
            read_be16(__payload + 0) != ARP_HW_ETHERNET ||
            read_be16(__payload + 2) != bigos::net::ETHERNET_TYPE_IPV4 || __payload[4] != 6 || __payload[5] != 4) {
            __ctx->diagnostics.arp_malformed++;
            return bigos::net::Status::Malformed;
        }
        const uint16_t op = read_be16(__payload + 6);
        if (op != ARP_OP_REQUEST && op != ARP_OP_REPLY) {
            __ctx->diagnostics.arp_malformed++;
            return bigos::net::Status::Unsupported;
        }
        bigos::net::MacAddress sender_mac = {};
        memcpy(sender_mac.bytes, __payload + 8, 6);
        const bigos::net::Ipv4Address sender_ip = {read_be32(__payload + 14)};
        bigos::net::MacAddress target_mac = {};
        memcpy(target_mac.bytes, __payload + 18, 6);
        const bigos::net::Ipv4Address target_ip = {read_be32(__payload + 24)};
        if (mac_zero(sender_mac) || ipv4_zero(sender_ip)) {
            __ctx->diagnostics.arp_malformed++;
            return bigos::net::Status::Malformed;
        }
        const bigos::net::Status cache_status = update_arp(__ctx, sender_ip, sender_mac);
        if (cache_status != bigos::net::Status::Ok)
            return cache_status;
        if (op == ARP_OP_REQUEST && ipv4_equal(target_ip, __ctx->config.local_ipv4))
            return send_arp(__ctx, ARP_OP_REPLY, sender_mac, sender_ip, sender_mac);
        if (op == ARP_OP_REPLY && ipv4_equal(target_ip, __ctx->config.local_ipv4) &&
            bigos::net::mac_equal(target_mac, __ctx->config.local_mac))
            return bigos::net::Status::Ok;
        return bigos::net::Status::NotLocal;
    }

    bigos::net::Status handle_ethernet(bigos::net::Context *__ctx, const uint8_t *__frame,
                                       uint32_t __length) noexcept {
        if (__frame == nullptr || __length < bigos::net::ETHERNET_MIN_FRAME_LEN ||
            __length > bigos::net::ETHERNET_MAX_FRAME_LEN) {
            __ctx->diagnostics.eth_malformed++;
            return bigos::net::Status::Malformed;
        }
        if (!accepted_destination(__ctx, __frame)) {
            __ctx->diagnostics.eth_not_local++;
            return bigos::net::Status::NotLocal;
        }
        const uint16_t ethertype = read_be16(__frame + 12);
        const uint8_t *payload = __frame + bigos::net::ETHERNET_HEADER_LEN;
        const uint16_t payload_len = (uint16_t)(__length - bigos::net::ETHERNET_HEADER_LEN);
        if (ethertype == bigos::net::ETHERNET_TYPE_ARP)
            return handle_arp(__ctx, payload, payload_len);
        if (ethertype == bigos::net::ETHERNET_TYPE_IPV4)
            return handle_ipv4(__ctx, payload, payload_len);
        __ctx->diagnostics.eth_unsupported++;
        return bigos::net::Status::Unsupported;
    }

    void clear_endpoint(bigos::net::UdpEndpoint *__endpoint) noexcept {
        if (__endpoint == nullptr)
            return;
        *__endpoint = {};
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace net {
    Ipv4Address make_ipv4(uint8_t __a, uint8_t __b, uint8_t __c, uint8_t __d) noexcept {
        return {((uint32_t)__a << 24) | ((uint32_t)__b << 16) | ((uint32_t)__c << 8) | __d};
    }

    bool mac_equal(const MacAddress &__a, const MacAddress &__b) noexcept {
        for (uint32_t i = 0; i < sizeof(__a.bytes); i++) {
            if (__a.bytes[i] != __b.bytes[i])
                return false;
        }
        return true;
    }

    const char *status_name(Status __status) noexcept {
        switch (__status) {
            case Status::Ok:
                return "ok";
            case Status::Disabled:
                return "disabled";
            case Status::InvalidArgument:
                return "invalid-argument";
            case Status::InvalidConfig:
                return "invalid-config";
            case Status::NotReady:
                return "not-ready";
            case Status::UnsupportedContext:
                return "unsupported-context";
            case Status::Malformed:
                return "malformed";
            case Status::Unsupported:
                return "unsupported";
            case Status::NotLocal:
                return "not-local";
            case Status::TooLarge:
                return "too-large";
            case Status::NoRoute:
                return "no-route";
            case Status::ArpUnresolved:
                return "arp-unresolved";
            case Status::CacheFull:
                return "cache-full";
            case Status::TableFull:
                return "table-full";
            case Status::AlreadyBound:
                return "already-bound";
            case Status::NotBound:
                return "not-bound";
            case Status::QueueFull:
                return "queue-full";
            case Status::NoData:
                return "no-data";
            case Status::Timeout:
                return "timeout";
            case Status::DeviceTxFailure:
                return "device-tx-failure";
            default:
                return "unknown";
        }
    }

    Context *default_context() noexcept {
        return &g_default_context;
    }

    Status init(Context *__ctx, device::NetworkDevice *__device, const Config *__config) noexcept {
        if (__ctx == nullptr)
            return Status::InvalidArgument;
        *__ctx = {};
        if (!ordinary_context()) {
            __ctx->diagnostics.irq_context_rejected++;
            __ctx->state = State::Disabled;
            __ctx->diagnostics.last_status = Status::UnsupportedContext;
            return Status::UnsupportedContext;
        }
        if (__device == nullptr || __device->ready == nullptr || !__device->ready(__device)) {
            __ctx->state = State::SkippedNoDevice;
            __ctx->diagnostics.init_skipped_no_device++;
            __ctx->diagnostics.last_status = Status::NotReady;
            return Status::NotReady;
        }
        if (__config == nullptr || ipv4_zero(__config->local_ipv4) || mac_zero(__config->local_mac)) {
            __ctx->state = State::SkippedInvalidConfig;
            __ctx->diagnostics.init_skipped_invalid_config++;
            __ctx->diagnostics.last_status = Status::InvalidConfig;
            return Status::InvalidConfig;
        }
        __ctx->device = __device;
        __ctx->config = *__config;
        if (__ctx->config.mtu == 0)
            __ctx->config.mtu = __device->mtu == nullptr ? DEFAULT_MTU : __device->mtu(__device);
        if (__ctx->config.mtu == 0 || __ctx->config.mtu > DEFAULT_MTU)
            __ctx->config.mtu = DEFAULT_MTU;
        if (__device->mac != nullptr) {
            const uint8_t *mac = __device->mac(__device);
            if (mac != nullptr)
                memcpy(__ctx->config.local_mac.bytes, mac, sizeof(__ctx->config.local_mac.bytes));
        }
        __ctx->state = State::Ready;
        __ctx->diagnostics.init_ready++;
        __ctx->diagnostics.last_status = Status::Ok;
        return Status::Ok;
    }

    Status init_default(const Config *__config) noexcept {
        device::NetworkDevice *netdev = device::network(device::DeviceRole::VirtioNetValidation);
        return init(&g_default_context, netdev, __config);
    }

    State state(const Context *__ctx) noexcept {
        return __ctx == nullptr ? State::Disabled : __ctx->state;
    }

    void diagnostics(const Context *__ctx, Diagnostics *__out) noexcept {
        if (__ctx == nullptr || __out == nullptr)
            return;
        *__out = __ctx->diagnostics;
    }

    Status pump(Context *__ctx, uint32_t __max_frames) noexcept {
        if (!validate_ready(__ctx))
            return __ctx == nullptr ? Status::InvalidArgument : __ctx->diagnostics.last_status;
        if (__ctx->device->poll_rx == nullptr || __ctx->device->return_rx == nullptr)
            return Status::NotReady;
        Status last = Status::NoData;
        for (uint32_t i = 0; i < __max_frames; i++) {
            device::NetworkRxFrame rx = {};
            const device::NetworkRxStatus rx_status = __ctx->device->poll_rx(__ctx->device, &rx);
            if (rx_status == device::NetworkRxStatus::NoFrame)
                break;
            if (rx_status != device::NetworkRxStatus::Success) {
                set_status(__ctx, Status::NotReady);
                return Status::NotReady;
            }
            __ctx->diagnostics.rx_frames++;
            last = handle_ethernet(__ctx, rx.data, rx.length);
            const device::NetworkRxStatus ret = __ctx->device->return_rx(__ctx->device, &rx);
            if (ret == device::NetworkRxStatus::Success)
                __ctx->diagnostics.rx_returned++;
            else
                __ctx->diagnostics.rx_return_failed++;
        }
        set_status(__ctx, last);
        return last;
    }

    Status inject_frame(Context *__ctx, const uint8_t *__frame, uint32_t __length) noexcept {
        if (!validate_ready(__ctx))
            return __ctx == nullptr ? Status::InvalidArgument : __ctx->diagnostics.last_status;
        const Status status = handle_ethernet(__ctx, __frame, __length);
        set_status(__ctx, status);
        return status;
    }

    Status arp_resolve(Context *__ctx, Ipv4Address __ipv4, MacAddress *__out, timer::tick_t __timeout_ticks) noexcept {
        if (!validate_ready(__ctx))
            return __ctx == nullptr ? Status::InvalidArgument : __ctx->diagnostics.last_status;
        if (__out == nullptr || ipv4_zero(__ipv4))
            return Status::InvalidArgument;
        ArpEntry *entry = find_arp(__ctx, __ipv4);
        if (entry != nullptr) {
            *__out = entry->mac;
            return Status::Ok;
        }
        ArpPending *pending = nullptr;
        for (uint32_t i = 0; i < ARP_PENDING_CAPACITY; i++) {
            if (__ctx->arp_pending[i].active && ipv4_equal(__ctx->arp_pending[i].ipv4, __ipv4)) {
                pending = &__ctx->arp_pending[i];
                break;
            }
        }
        if (pending == nullptr) {
            for (uint32_t i = 0; i < ARP_PENDING_CAPACITY; i++) {
                if (!__ctx->arp_pending[i].active) {
                    pending = &__ctx->arp_pending[i];
                    pending->active = true;
                    pending->ipv4 = __ipv4;
                    pending->deadline_tick = timer::ticks() + (__timeout_ticks == 0 ? 1 : __timeout_ticks);
                    MacAddress zero = {};
                    (void)send_arp(__ctx, ARP_OP_REQUEST, BROADCAST_MAC, __ipv4, zero);
                    break;
                }
            }
        }
        if (pending == nullptr) {
            __ctx->diagnostics.arp_cache_full++;
            return Status::CacheFull;
        }
        if (__timeout_ticks == 0) {
            __ctx->diagnostics.arp_unresolved++;
            return Status::ArpUnresolved;
        }
        const timer::tick_t start = timer::ticks();
        while (timer::ticks() - start < __timeout_ticks) {
            (void)pump(__ctx, 1);
            entry = find_arp(__ctx, __ipv4);
            if (entry != nullptr) {
                pending->active = false;
                *__out = entry->mac;
                return Status::Ok;
            }
            sched::yield();
        }
        pending->active = false;
        __ctx->diagnostics.arp_unresolved++;
        return Status::Timeout;
    }

    Status udp_bind(Context *__ctx, uint16_t __local_port, UdpEndpoint **__out) noexcept {
        if (!validate_ready(__ctx))
            return __ctx == nullptr ? Status::InvalidArgument : __ctx->diagnostics.last_status;
        if (__local_port == 0 || __out == nullptr)
            return Status::InvalidArgument;
        *__out = nullptr;
        if (find_udp_endpoint(__ctx, __local_port) != nullptr)
            return Status::AlreadyBound;
        for (uint32_t i = 0; i < UDP_ENDPOINT_CAPACITY; i++) {
            UdpEndpoint &endpoint = __ctx->udp_endpoints[i];
            if (!endpoint.active) {
                clear_endpoint(&endpoint);
                endpoint.active = true;
                endpoint.local_port = __local_port;
                *__out = &endpoint;
                __ctx->diagnostics.udp_bound++;
                return Status::Ok;
            }
        }
        return Status::TableFull;
    }

    Status udp_close(Context *__ctx, UdpEndpoint *__endpoint) noexcept {
        if (__ctx == nullptr || __endpoint == nullptr)
            return Status::InvalidArgument;
        for (uint32_t i = 0; i < UDP_ENDPOINT_CAPACITY; i++) {
            if (&__ctx->udp_endpoints[i] == __endpoint) {
                clear_endpoint(__endpoint);
                return Status::Ok;
            }
        }
        return Status::NotBound;
    }

    Status udp_send_to(Context *__ctx, UdpEndpoint *__endpoint, Ipv4Address __destination_ipv4,
                       uint16_t __destination_port, const uint8_t *__payload, uint16_t __payload_length,
                       timer::tick_t) noexcept {
        if (!validate_ready(__ctx))
            return __ctx == nullptr ? Status::InvalidArgument : __ctx->diagnostics.last_status;
        if (__endpoint == nullptr || !__endpoint->active || __destination_port == 0)
            return Status::NotBound;
        if (__payload == nullptr && __payload_length != 0)
            return Status::InvalidArgument;
        if (__payload_length > UDP_MAX_PAYLOAD)
            return Status::TooLarge;
        const uint16_t udp_len = (uint16_t)(8 + __payload_length);
        uint8_t udp[8 + UDP_MAX_PAYLOAD] = {};
        write_be16(udp + 0, __endpoint->local_port);
        write_be16(udp + 2, __destination_port);
        write_be16(udp + 4, udp_len);
        write_be16(udp + 6, 0);
        if (__payload_length != 0)
            memcpy(udp + 8, __payload, __payload_length);

        uint8_t pseudo[12 + 8 + UDP_MAX_PAYLOAD] = {};
        write_be32(pseudo + 0, __ctx->config.local_ipv4.value);
        write_be32(pseudo + 4, __destination_ipv4.value);
        pseudo[8] = 0;
        pseudo[9] = IPV4_PROTOCOL_UDP;
        write_be16(pseudo + 10, udp_len);
        memcpy(pseudo + 12, udp, udp_len);
        uint16_t checksum = internet_checksum(pseudo, 12 + udp_len);
        if (checksum == 0)
            checksum = 0xffffu;
        write_be16(udp + 6, checksum);

        const Status status = send_ipv4(__ctx, __destination_ipv4, IPV4_PROTOCOL_UDP, udp, udp_len);
        if (status == Status::Ok)
            __ctx->diagnostics.udp_sent++;
        return status;
    }

    Status udp_receive_from(UdpEndpoint *__endpoint, UdpDatagram *__out) noexcept {
        if (__endpoint == nullptr || __out == nullptr)
            return Status::InvalidArgument;
        if (!ordinary_context())
            return Status::UnsupportedContext;
        if (!__endpoint->active)
            return Status::NotBound;
        if (__endpoint->rx_count == 0)
            return Status::NoData;
        *__out = __endpoint->rx_queue[__endpoint->rx_head];
        __endpoint->rx_head = (__endpoint->rx_head + 1) % UDP_RX_QUEUE_CAPACITY;
        __endpoint->rx_count--;
        return Status::Ok;
    }

#ifdef BIGOS_NETWORK_PROTOCOL_SMOKE
    namespace {
        struct FakeDevice {
            device::NetworkDevice net;
            MacAddress mac;
            uint8_t tx[ETHERNET_MAX_FRAME_LEN];
            uint32_t tx_len;
            uint32_t tx_count;
        };

        FakeDevice g_smoke_fake = {};
        Context g_smoke_context = {};
        uint8_t g_smoke_frame[ETHERNET_MAX_FRAME_LEN] = {};

        bool fake_ready(device::NetworkDevice *__device) noexcept {
            return __device != nullptr;
        }

        bool fake_link_up(device::NetworkDevice *__device) noexcept {
            return __device != nullptr;
        }

        const uint8_t *fake_mac(device::NetworkDevice *__device) noexcept {
            FakeDevice *fake = __device == nullptr ? nullptr : (FakeDevice *)__device->context;
            return fake == nullptr ? nullptr : fake->mac.bytes;
        }

        uint16_t fake_mtu(device::NetworkDevice *) noexcept {
            return DEFAULT_MTU;
        }

        device::NetworkTxStatus fake_transmit(
            device::NetworkDevice *__device, const void *__frame, uint32_t __len, uint64_t) noexcept {
            FakeDevice *fake = __device == nullptr ? nullptr : (FakeDevice *)__device->context;
            if (fake == nullptr || __frame == nullptr || __len > ETHERNET_MAX_FRAME_LEN)
                return device::NetworkTxStatus::InvalidFrame;
            memcpy(fake->tx, __frame, __len);
            fake->tx_len = __len;
            fake->tx_count++;
            return device::NetworkTxStatus::Success;
        }

        device::NetworkRxStatus fake_poll_rx(device::NetworkDevice *, device::NetworkRxFrame *) noexcept {
            return device::NetworkRxStatus::NoFrame;
        }

        device::NetworkRxStatus fake_return_rx(device::NetworkDevice *, const device::NetworkRxFrame *) noexcept {
            return device::NetworkRxStatus::Success;
        }

        void fake_diagnostics(device::NetworkDevice *, device::NetworkDiagnostics *) noexcept {}

        void fill_eth(uint8_t *__frame, const MacAddress &__dst, const MacAddress &__src, uint16_t __type) noexcept {
            memcpy(__frame, __dst.bytes, 6);
            memcpy(__frame + 6, __src.bytes, 6);
            write_be16(__frame + 12, __type);
        }

        uint16_t build_ipv4_udp(uint8_t *__out, Ipv4Address __src, Ipv4Address __dst, uint16_t __src_port,
                                uint16_t __dst_port, const uint8_t *__payload, uint16_t __payload_len) noexcept {
            const uint16_t udp_len = (uint16_t)(8 + __payload_len);
            const uint16_t total_len = (uint16_t)(20 + udp_len);
            memset(__out, 0, total_len);
            __out[0] = (IPV4_VERSION << 4) | IPV4_MIN_IHL;
            write_be16(__out + 2, total_len);
            __out[8] = IPV4_TTL;
            __out[9] = IPV4_PROTOCOL_UDP;
            write_be32(__out + 12, __src.value);
            write_be32(__out + 16, __dst.value);
            write_be16(__out + 10, internet_checksum(__out, 20));
            uint8_t *udp = __out + 20;
            write_be16(udp + 0, __src_port);
            write_be16(udp + 2, __dst_port);
            write_be16(udp + 4, udp_len);
            write_be16(udp + 6, 0);
            memcpy(udp + 8, __payload, __payload_len);
            return total_len;
        }

        uint16_t build_ipv4_icmp_echo(uint8_t *__out, Ipv4Address __src, Ipv4Address __dst) noexcept {
            memset(__out, 0, 30);
            uint8_t *icmp = __out + 20;
            icmp[0] = ICMP_ECHO_REQUEST;
            icmp[1] = 0;
            write_be16(icmp + 4, 0x1234);
            write_be16(icmp + 6, 1);
            icmp[8] = 0xaa;
            icmp[9] = 0xbb;
            write_be16(icmp + 2, internet_checksum(icmp, 10));
            const uint16_t total_len = 30;
            __out[0] = (IPV4_VERSION << 4) | IPV4_MIN_IHL;
            write_be16(__out + 2, total_len);
            __out[8] = IPV4_TTL;
            __out[9] = IPV4_PROTOCOL_ICMP;
            write_be32(__out + 12, __src.value);
            write_be32(__out + 16, __dst.value);
            write_be16(__out + 10, internet_checksum(__out, 20));
            return total_len;
        }

        void print_smoke_fail(const char *__reason) noexcept {
            bigos::serial_puts("BIGOS_NETWORK_PROTOCOL_FAILED ");
            bigos::serial_puts(__reason);
            bigos::serial_puts("\n");
        }
    }   // namespace

    void protocol_smoke_entry(void *) noexcept {
        g_smoke_fake = {};
        g_smoke_context = {};
        g_smoke_fake.mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
        g_smoke_fake.net.context = &g_smoke_fake;
        g_smoke_fake.net.ready = &fake_ready;
        g_smoke_fake.net.link_up = &fake_link_up;
        g_smoke_fake.net.mac = &fake_mac;
        g_smoke_fake.net.mtu = &fake_mtu;
        g_smoke_fake.net.transmit = &fake_transmit;
        g_smoke_fake.net.poll_rx = &fake_poll_rx;
        g_smoke_fake.net.return_rx = &fake_return_rx;
        g_smoke_fake.net.diagnostics = &fake_diagnostics;

        Config config = {};
        config.local_ipv4 = make_ipv4(10, 0, 2, 15);
        config.netmask = make_ipv4(255, 255, 255, 0);
        config.direct_peer_ipv4 = make_ipv4(10, 0, 2, 2);
        config.local_mac = g_smoke_fake.mac;
        config.mtu = DEFAULT_MTU;
        config.has_direct_peer = true;

        if (init(&g_smoke_context, &g_smoke_fake.net, &config) != Status::Ok) {
            print_smoke_fail("init");
            return;
        }
        UdpEndpoint *endpoint = nullptr;
        if (udp_bind(&g_smoke_context, 4000, &endpoint) != Status::Ok) {
            print_smoke_fail("udp-bind");
            return;
        }
        UdpEndpoint *duplicate = endpoint;
        if (udp_bind(&g_smoke_context, 4000, &duplicate) != Status::AlreadyBound) {
            print_smoke_fail("duplicate-bind");
            return;
        }

        const MacAddress peer_mac = {{0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc}};
        uint8_t *frame = g_smoke_frame;
        fill_eth(frame, BROADCAST_MAC, peer_mac, ETHERNET_TYPE_ARP);
        uint8_t *arp = frame + ETHERNET_HEADER_LEN;
        write_be16(arp + 0, ARP_HW_ETHERNET);
        write_be16(arp + 2, ETHERNET_TYPE_IPV4);
        arp[4] = 6;
        arp[5] = 4;
        write_be16(arp + 6, ARP_OP_REQUEST);
        memcpy(arp + 8, peer_mac.bytes, 6);
        write_be32(arp + 14, make_ipv4(10, 0, 2, 2).value);
        write_be32(arp + 24, config.local_ipv4.value);
        if (inject_frame(&g_smoke_context, frame, ETHERNET_HEADER_LEN + ARP_PACKET_LEN) != Status::Ok ||
            g_smoke_fake.tx_count == 0) {
            print_smoke_fail("arp");
            return;
        }

        fill_eth(frame, config.local_mac, peer_mac, ETHERNET_TYPE_IPV4);
        const uint8_t ping_len = build_ipv4_icmp_echo(frame + ETHERNET_HEADER_LEN, make_ipv4(10, 0, 2, 2),
                                                      config.local_ipv4);
        if (inject_frame(&g_smoke_context, frame, ETHERNET_HEADER_LEN + ping_len) != Status::Ok) {
            print_smoke_fail("icmp");
            return;
        }

        const uint8_t msg[] = {'o', 'k'};
        fill_eth(frame, config.local_mac, peer_mac, ETHERNET_TYPE_IPV4);
        const uint16_t udp_ip_len = build_ipv4_udp(
            frame + ETHERNET_HEADER_LEN, make_ipv4(10, 0, 2, 2), config.local_ipv4, 5000, 4000, msg, sizeof(msg));
        if (inject_frame(&g_smoke_context, frame, ETHERNET_HEADER_LEN + udp_ip_len) != Status::Ok) {
            print_smoke_fail("udp-rx");
            return;
        }
        UdpDatagram datagram = {};
        if (udp_receive_from(endpoint, &datagram) != Status::Ok || datagram.payload_length != sizeof(msg) ||
            datagram.payload[0] != 'o' || datagram.payload[1] != 'k') {
            print_smoke_fail("udp-receive");
            return;
        }
        if (udp_send_to(&g_smoke_context, endpoint, make_ipv4(10, 0, 2, 2), 5000, msg, sizeof(msg), 0) !=
            Status::Ok) {
            print_smoke_fail("udp-send");
            return;
        }
        frame[12] = 0x88;
        frame[13] = 0xb5;
        if (inject_frame(&g_smoke_context, frame, ETHERNET_HEADER_LEN) != Status::Unsupported) {
            print_smoke_fail("unsupported");
            return;
        }
        bigos::serial_puts("BIGOS_NETWORK_PROTOCOL_PASSED\n");
    }
#endif
}   // namespace net
NAMESPACE_BIGOS_END
