from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/kernel.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def test_net_header_loopback_constants_and_appended_counters() -> None:
    header = read_source('include/bigos/net.h')

    # Loopback address recognition constants (append-only after existing consts).
    assert 'IPV4_LOOPBACK = 0x7f000001u' in header
    assert 'IPV4_LOOPBACK_PREFIX = 0x7f000000u' in header
    assert 'IPV4_LOOPBACK_MASK = 0xff000000u' in header

    # Existing capacity constants keep their values.
    assert 'UDP_RX_QUEUE_CAPACITY = 4' in header
    assert 'UDP_ENDPOINT_CAPACITY = 4' in header

    # Appended loopback diagnostics counters, immediately before last_status.
    delivered = header.index('loopback_delivered')
    dropped = header.index('loopback_dropped')
    last_status = header.index('Status last_status;')
    overflow = header.index('udp_payload_overflow')
    assert overflow < delivered < dropped < last_status

    # Offset guards keep the historical layout and append order.
    assert '__builtin_offsetof(Diagnostics, init_ready) == 0' in header
    assert '__builtin_offsetof(Diagnostics, udp_payload_overflow) == 32 * sizeof(uint32_t)' in header
    assert 'loopback_delivered) > __builtin_offsetof(Diagnostics, udp_payload_overflow)' in header
    assert 'loopback_dropped) > __builtin_offsetof(Diagnostics, loopback_delivered)' in header
    assert 'last_status) > __builtin_offsetof(Diagnostics, loopback_dropped)' in header


def test_net_header_appends_loopback_ready_state() -> None:
    header = read_source('include/bigos/net.h')

    # State enum keeps existing entries and appends LoopbackReady last.
    enum_start = header.index('enum class State')
    enum_end = header.index('};', enum_start)
    body = header[enum_start:enum_end]
    for name in ('Disabled', 'Ready', 'SkippedNoDevice', 'SkippedInvalidConfig', 'LoopbackReady'):
        assert name in body
    assert body.index('SkippedInvalidConfig') < body.index('LoopbackReady')


def test_protocol_source_loopback_split_and_normalization() -> None:
    source = read_source('kernel/core/net/protocol.cc')

    # Read-only local-address predicate covering the whole 127.0.0.0/8 block.
    assert 'bool is_local_delivery(' in source
    assert '(__dest.value & bigos::net::IPV4_LOOPBACK_MASK) == bigos::net::IPV4_LOOPBACK_PREFIX' in source

    # send_ipv4 classifies once, delivers local traffic through handle_ipv4, and
    # counts loopback deliveries/drops without touching transmit for local dests.
    send = source[source.index('Status send_ipv4('):source.index('Status handle_icmp(')]
    assert 'is_local_delivery(__ctx, __destination)' in send
    assert 'handle_ipv4(__ctx, packet, total_len)' in send
    assert 'loopback_delivered++' in send
    assert 'loopback_dropped++' in send
    # Outbound path stays gated on a ready device and keeps ARP + transmit.
    assert 'State::Ready || __ctx->device == nullptr' in send
    assert 'arp_resolve(__ctx, route_destination(__ctx, __destination)' in send
    assert 'transmit_ethernet(__ctx, dst_mac, bigos::net::ETHERNET_TYPE_IPV4' in send

    # handle_ipv4 accepts the local-address set plus broadcast; non-local dropped.
    assert '!is_local_delivery(__ctx, dest) && dest.value != BROADCAST_IPV4' in source
    assert 'ipv4_not_local++' in source

    # ICMP echo reply is a terminal accept (bounded echo-to-self recursion).
    icmp = source[source.index('Status handle_icmp('):source.index('find_udp_endpoint(')]
    assert 'ICMP_ECHO_REPLY && __payload[1] == 0' in icmp
    assert 'return bigos::net::Status::Ok;' in icmp

    # UDP send normalizes the checksum destination for local delivery.
    assert 'is_local_delivery(__ctx, __destination_ipv4) ? __ctx->config.local_ipv4 : __destination_ipv4' in source


def test_protocol_source_loopback_readiness_gate() -> None:
    source = read_source('kernel/core/net/protocol.cc')

    # Loopback-only readiness gate accepts Ready+device or LoopbackReady.
    assert 'bool validate_local_ready(' in source
    gate = source[source.index('bool validate_local_ready('):source.index('bigos::net::ArpEntry *find_arp(')]
    assert 'State::Ready && __ctx->device != nullptr' in gate
    assert 'State::LoopbackReady' in gate

    # bind/send use the loopback-capable gate; frame paths stay device-strict.
    assert 'Status udp_bind(Context *__ctx, uint16_t __local_port, UdpEndpoint **__out) noexcept {\n        if (!validate_local_ready(__ctx))' in source
    assert 'timer::tick_t) noexcept {\n        if (!validate_local_ready(__ctx))' in source
    assert 'Status pump(Context *__ctx, uint32_t __max_frames) noexcept {\n        if (!validate_ready(__ctx))' in source

    # init enters LoopbackReady when config is valid but no device is ready.
    init = source[source.index('Status init(Context *__ctx'):source.index('Status init_default(')]
    assert 'State::LoopbackReady' in init
    assert 'State::SkippedNoDevice' in init


def test_loopback_smoke_and_build_switch_are_default_off() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')
    protocol = read_source('kernel/core/net/protocol.cc')
    header = read_source('include/bigos/net.h')

    assert 'option("loopback_network_smoke")' in xmake
    option_index = xmake.index('option("loopback_network_smoke")')
    assert 'set_default(false)' in xmake[option_index:option_index + 240]
    assert 'add_defines("BIGOS_LOOPBACK_NETWORK_SMOKE")' in xmake

    assert '#ifdef BIGOS_LOOPBACK_NETWORK_SMOKE' in header
    assert 'void loopback_network_smoke_entry(void *) noexcept;' in header

    assert '#ifdef BIGOS_LOOPBACK_NETWORK_SMOKE' in protocol
    assert 'BIGOS_LOOPBACK_NETWORK_PASSED' in protocol
    assert 'BIGOS_LOOPBACK_NETWORK_FAILED' in protocol
    # Smoke covers UDP loopback, error paths, and ICMP echo-to-self.
    assert 'state(&g_loopback_context) != State::LoopbackReady' in protocol
    assert 'Status::QueueFull' in protocol
    assert 'Status::NotReady' in protocol
    assert 'icmp_echo_replies != icmp_rep_before + 1' in protocol

    assert 'create_kernel_thread(&bigos::net::loopback_network_smoke_entry' in kernel


def test_change_does_not_add_syscall_or_socket_abi() -> None:
    syscall_h = read_source('include/bigos/syscall.h')
    # No loopback-specific user-visible ABI is introduced by this change.
    assert 'LOOPBACK' not in syscall_h
