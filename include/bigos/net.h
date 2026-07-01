#ifndef _BIGOS_NET_H
#define _BIGOS_NET_H

#include <bigos/device.h>
#include <bigos/sched.h>
#include <bigos/timer.h>
#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace net {
    constexpr uint16_t ETHERNET_HEADER_LEN = 14;
    constexpr uint16_t ETHERNET_MIN_FRAME_LEN = 14;
    constexpr uint16_t ETHERNET_MAX_FRAME_LEN = 1518;
    constexpr uint16_t ETHERNET_TYPE_ARP = 0x0806;
    constexpr uint16_t ETHERNET_TYPE_IPV4 = 0x0800;
    constexpr uint16_t ARP_PACKET_LEN = 28;
    constexpr uint8_t IPV4_PROTOCOL_ICMP = 1;
    constexpr uint8_t IPV4_PROTOCOL_UDP = 17;
    constexpr uint16_t DEFAULT_MTU = 1500;
    constexpr uint32_t ARP_CACHE_CAPACITY = 4;
    constexpr uint32_t ARP_PENDING_CAPACITY = 4;
    constexpr uint32_t UDP_ENDPOINT_CAPACITY = 4;
    constexpr uint32_t UDP_RX_QUEUE_CAPACITY = 4;
    constexpr uint32_t UDP_MAX_PAYLOAD = 512;
    // Local-address (loopback) recognition. The loopback network is the whole
    // 127.0.0.0/8 block, not only 127.0.0.1: a destination is a loopback address
    // when (value & IPV4_LOOPBACK_MASK) == IPV4_LOOPBACK_PREFIX.
    constexpr uint32_t IPV4_LOOPBACK = 0x7f000001u;         // 127.0.0.1
    constexpr uint32_t IPV4_LOOPBACK_PREFIX = 0x7f000000u;  // 127.0.0.0
    constexpr uint32_t IPV4_LOOPBACK_MASK = 0xff000000u;    // /8

    struct MacAddress {
        uint8_t bytes[6];
    };

    struct Ipv4Address {
        uint32_t value;
    };

    enum class State : uint8_t {
        Disabled,
        Ready,
        SkippedNoDevice,
        SkippedInvalidConfig,
        // Appended: valid local IPv4 config but no ready frame-level device. The
        // local-address (loopback) delivery path is usable; outbound (non-local)
        // transmission stays gated on a ready device.
        LoopbackReady,
    };

    enum class Status : uint8_t {
        Ok,
        Disabled,
        InvalidArgument,
        InvalidConfig,
        NotReady,
        UnsupportedContext,
        Malformed,
        Unsupported,
        NotLocal,
        TooLarge,
        NoRoute,
        ArpUnresolved,
        CacheFull,
        TableFull,
        AlreadyBound,
        NotBound,
        QueueFull,
        NoData,
        Timeout,
        DeviceTxFailure,
    };

    struct Config {
        Ipv4Address local_ipv4;
        Ipv4Address netmask;
        Ipv4Address gateway_ipv4;
        Ipv4Address direct_peer_ipv4;
        MacAddress local_mac;
        uint16_t mtu;
        bool has_gateway;
        bool has_direct_peer;
    };

    struct Diagnostics {
        uint32_t init_ready;
        uint32_t init_skipped_no_device;
        uint32_t init_skipped_invalid_config;
        uint32_t irq_context_rejected;
        uint32_t rx_frames;
        uint32_t rx_returned;
        uint32_t rx_return_failed;
        uint32_t tx_frames;
        uint32_t tx_failed;
        uint32_t eth_malformed;
        uint32_t eth_not_local;
        uint32_t eth_unsupported;
        uint32_t arp_requests;
        uint32_t arp_replies;
        uint32_t arp_malformed;
        uint32_t arp_cache_full;
        uint32_t arp_unresolved;
        uint32_t ipv4_rx;
        uint32_t ipv4_malformed;
        uint32_t ipv4_bad_checksum;
        uint32_t ipv4_fragmented;
        uint32_t ipv4_not_local;
        uint32_t ipv4_unsupported_protocol;
        uint32_t icmp_echo_requests;
        uint32_t icmp_echo_replies;
        uint32_t icmp_malformed;
        uint32_t udp_bound;
        uint32_t udp_sent;
        uint32_t udp_received;
        uint32_t udp_unbound;
        uint32_t udp_queue_full;
        uint32_t udp_malformed;
        uint32_t udp_payload_overflow;
        // Append-only loopback counters. loopback_delivered counts local-address
        // packets that reached the local input path successfully (UDP enqueued or
        // ICMP echo handled); loopback_dropped counts local-address packets the
        // loopback split dropped (unbound, queue full, or checksum/validation
        // failure). They let validation distinguish a local-loopback hit from an
        // outbound frame-level device hit without inspecting device counters.
        uint32_t loopback_delivered;
        uint32_t loopback_dropped;
        Status last_status;
    };

    // Guard the existing Diagnostics layout: the historical counters keep their
    // offsets and the new loopback counters are appended immediately before
    // last_status, so consumers that read prior fields are unaffected.
    static_assert(__builtin_offsetof(Diagnostics, init_ready) == 0, "Diagnostics init_ready moved");
    static_assert(__builtin_offsetof(Diagnostics, udp_payload_overflow) == 32 * sizeof(uint32_t),
                  "Diagnostics existing counter layout changed");
    static_assert(__builtin_offsetof(Diagnostics, loopback_delivered) > __builtin_offsetof(Diagnostics, udp_payload_overflow),
                  "loopback_delivered must be appended after existing counters");
    static_assert(__builtin_offsetof(Diagnostics, loopback_dropped) > __builtin_offsetof(Diagnostics, loopback_delivered),
                  "loopback_dropped must follow loopback_delivered");
    static_assert(__builtin_offsetof(Diagnostics, last_status) > __builtin_offsetof(Diagnostics, loopback_dropped),
                  "last_status must stay after the appended loopback counters");

    struct UdpDatagram {
        Ipv4Address source_ipv4;
        uint16_t source_port;
        uint16_t payload_length;
        uint8_t payload[UDP_MAX_PAYLOAD];
    };

    struct UdpEndpoint {
        bool active;
        uint16_t local_port;
        UdpDatagram rx_queue[UDP_RX_QUEUE_CAPACITY];
        uint32_t rx_head;
        uint32_t rx_count;
        // Receive wait queue. The protocol RX delivery path wakes it after a
        // datagram is enqueued so the unified readiness model and future
        // multiplexing have a wakeup source. Reset on bind via clear_endpoint().
        sched::WaitQueue rx_wait;
    };

    struct ArpEntry {
        bool valid;
        Ipv4Address ipv4;
        MacAddress mac;
        timer::tick_t updated_tick;
    };

    struct ArpPending {
        bool active;
        Ipv4Address ipv4;
        timer::tick_t deadline_tick;
    };

    struct Context {
        State state;
        device::NetworkDevice *device;
        Config config;
        Diagnostics diagnostics;
        ArpEntry arp_cache[ARP_CACHE_CAPACITY];
        ArpPending arp_pending[ARP_PENDING_CAPACITY];
        UdpEndpoint udp_endpoints[UDP_ENDPOINT_CAPACITY];
        uint8_t tx_frame[ETHERNET_MAX_FRAME_LEN];
        uint16_t ipv4_identification;
    };

    Ipv4Address make_ipv4(uint8_t __a, uint8_t __b, uint8_t __c, uint8_t __d) noexcept;
    bool mac_equal(const MacAddress &__a, const MacAddress &__b) noexcept;
    const char *status_name(Status __status) noexcept;

    Context *default_context() noexcept;
    Status init(Context *__ctx, device::NetworkDevice *__device, const Config *__config) noexcept;
    Status init_default(const Config *__config) noexcept;
    State state(const Context *__ctx) noexcept;
    void diagnostics(const Context *__ctx, Diagnostics *__out) noexcept;

    Status pump(Context *__ctx, uint32_t __max_frames) noexcept;
    Status inject_frame(Context *__ctx, const uint8_t *__frame, uint32_t __length) noexcept;
    Status arp_resolve(Context *__ctx, Ipv4Address __ipv4, MacAddress *__out, timer::tick_t __timeout_ticks) noexcept;

    Status udp_bind(Context *__ctx, uint16_t __local_port, UdpEndpoint **__out) noexcept;
    Status udp_close(Context *__ctx, UdpEndpoint *__endpoint) noexcept;
    Status udp_send_to(Context *__ctx, UdpEndpoint *__endpoint, Ipv4Address __destination_ipv4,
                       uint16_t __destination_port, const uint8_t *__payload, uint16_t __payload_length,
                       timer::tick_t __timeout_ticks) noexcept;
    Status udp_receive_from(UdpEndpoint *__endpoint, UdpDatagram *__out) noexcept;

#ifdef BIGOS_NETWORK_PROTOCOL_SMOKE
    void protocol_smoke_entry(void *) noexcept;
#endif

#ifdef BIGOS_LOOPBACK_NETWORK_SMOKE
    // Default-off kernel-internal local-address (loopback) closed-loop smoke. It
    // initializes a context with local IPv4 config and no frame-level device
    // (LoopbackReady), then exercises the UDP loopback closed loop, unbound/
    // queue-full/non-local error paths, and an ICMP echo-to-self, emitting a
    // deterministic BIGOS_LOOPBACK_NETWORK_PASSED / BIGOS_LOOPBACK_NETWORK_FAILED
    // marker without any real tap/network backend.
    void loopback_network_smoke_entry(void *) noexcept;
#endif
}   // namespace net
NAMESPACE_BIGOS_END

#endif   // _BIGOS_NET_H
