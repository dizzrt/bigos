import re
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


def test_net_header_tcp_protocol_and_capacity_constants() -> None:
    header = read_source('include/bigos/net.h')

    # TCP protocol number appended near the existing ICMP/UDP constants.
    assert 'IPV4_PROTOCOL_TCP = 6' in header
    icmp = header.index('IPV4_PROTOCOL_ICMP = 1')
    udp = header.index('IPV4_PROTOCOL_UDP = 17')
    tcp = header.index('IPV4_PROTOCOL_TCP = 6')
    assert icmp < udp < tcp

    # Bounded TCP capacities/timers exist as compile-time constants.
    for name in (
        'TCP_CONNECTION_CAPACITY',
        'TCP_MSS',
        'TCP_SEND_BUFFER',
        'TCP_RECV_BUFFER',
        'TCP_RETX_QUEUE_CAPACITY',
        'TCP_REORDER_SLOTS',
        'TCP_RTO_MIN',
        'TCP_RTO_MAX',
        'TCP_MSL_TICKS',
        'TCP_MAX_RETRANSMIT',
    ):
        assert f'{name} =' in header, name

    # MSS upper-bound relationship is guarded at compile time against the shared
    # send_ipv4 IPv4 payload buffer and the default MTU.
    assert 'static_assert(20 + 20 + TCP_MSS <= 20 + UDP_MAX_PAYLOAD + 8' in header
    assert 'static_assert(20 + 20 + TCP_MSS <= DEFAULT_MTU' in header


def test_net_header_appends_tcp_diagnostics_counters() -> None:
    header = read_source('include/bigos/net.h')

    # Appended TCP counters land after the loopback counters, before last_status.
    dropped = header.index('loopback_dropped')
    seg_rx = header.index('tcp_segments_rx')
    tcp_dropped = header.index('tcp_dropped')
    last_status = header.index('Status last_status;')
    assert dropped < seg_rx < tcp_dropped < last_status

    for name in (
        'tcp_segments_rx',
        'tcp_segments_tx',
        'tcp_retransmits',
        'tcp_connections_opened',
        'tcp_connections_closed',
        'tcp_resets',
        'tcp_dropped',
    ):
        assert name in header, name

    # Offset guards keep the append-only layout intact.
    assert 'tcp_segments_rx) > __builtin_offsetof(Diagnostics, loopback_dropped)' in header
    assert 'tcp_dropped) > __builtin_offsetof(Diagnostics, tcp_segments_rx)' in header
    assert 'last_status) > __builtin_offsetof(Diagnostics, tcp_dropped)' in header
    # Existing loopback offset guards are untouched.
    assert 'loopback_delivered) > __builtin_offsetof(Diagnostics, udp_payload_overflow)' in header


def test_tcp_header_declares_state_machine_and_internal_api() -> None:
    header = read_source('include/bigos/net/tcp.h')

    # State enum covers the full bounded transition set.
    enum_start = header.index('enum class TcpState')
    enum_end = header.index('};', enum_start)
    body = header[enum_start:enum_end]
    for name in (
        'Closed',
        'Listen',
        'SynSent',
        'SynReceived',
        'Established',
        'FinWait1',
        'FinWait2',
        'Closing',
        'CloseWait',
        'LastAck',
        'TimeWait',
    ):
        assert name in body, name

    # Internal API: open/listen/send/receive/close plus input and pump entries.
    for decl in (
        'Status tcp_open(',
        'Status tcp_listen(',
        'Status tcp_send(',
        'Status tcp_receive(',
        'Status tcp_close(',
        'Status handle_tcp(',
        'Status tcp_pump(',
    ):
        assert decl in header, decl

    # RFC 6298 estimator state and bounded storage live in the TCB.
    for field in ('srtt', 'rttvar', 'rto', 'send_buffer[TCP_SEND_BUFFER]',
                  'recv_buffer[TCP_RECV_BUFFER]', 'retx[TCP_RETX_QUEUE_CAPACITY]',
                  'reorder[TCP_REORDER_SLOTS]', 'time_wait_deadline_tick'):
        assert field in header, field


def test_tcp_source_reuses_ipv4_output_and_rfc6298() -> None:
    source = read_source('kernel/core/net/tcp.cc')

    # TCP segments go through the shared IPv4 output forwarder, not a second stack.
    assert 'ipv4_send(__ctx, __tcb->remote_ip, bigos::net::IPV4_PROTOCOL_TCP' in source
    assert 'tcp_segments_tx++' in source

    # Wraparound-safe sequence comparison via signed difference.
    assert '(int32_t)(__a - __b) < 0' in source

    # RFC 6298 estimator uses integer shifts (alpha=1/8, beta=1/4), no float.
    assert '__tcb->rttvar = __tcb->rttvar - (__tcb->rttvar >> 2) + (err >> 2)' in source
    assert '__tcb->srtt = __tcb->srtt - (__tcb->srtt >> 3) + (__r >> 3)' in source
    assert 'clamp_rto' in source
    # No floating-point arithmetic types (freestanding, no FPU dependency). Match
    # declared types with a following identifier so comment words like "doubled"
    # do not trip the check.
    assert not re.search(r'\b(float|double)\s+[A-Za-z_]', source)

    # Exponential backoff doubles rto, bounded by TCP_RTO_MAX; Karn excludes
    # retransmitted segments from RTT sampling.
    assert 'clamp_rto(tcb.rto << 1)' in source
    assert 'if (!e.retransmitted)' in source

    # Standard 2*MSL TIME_WAIT.
    assert '2 * bigos::net::TCP_MSL_TICKS' in source

    # Deterministic table-full on a full bounded TCB pool.
    assert 'tcp_dropped++' in source
    assert 'Status::TableFull' in source


def test_protocol_dispatch_adds_tcp_branch() -> None:
    source = read_source('kernel/core/net/protocol.cc')

    # handle_ipv4 gains a protocol-6 branch that counts RX and calls handle_tcp,
    # keeping the existing ICMP/UDP dispatch and unsupported semantics.
    assert 'protocol == bigos::net::IPV4_PROTOCOL_TCP' in source
    assert 'tcp_segments_rx++' in source
    assert 'bigos::net::handle_tcp(__ctx, source, dest, body, body_len)' in source
    assert 'ipv4_unsupported_protocol++' in source

    # The kernel-internal IPv4 output forwarder reuses send_ipv4.
    assert 'Status ipv4_send(' in source
    assert 'return send_ipv4(__ctx, __destination, __protocol, __payload, __payload_length);' in source


def test_tcp_smoke_and_switch_default_off() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')
    tcp = read_source('kernel/core/net/tcp.cc')

    assert 'option("tcp_path_smoke")' in xmake
    option_index = xmake.index('option("tcp_path_smoke")')
    assert 'set_default(false)' in xmake[option_index:option_index + 260]
    assert 'add_defines("BIGOS_TCP_PATH_SMOKE")' in xmake

    assert '#ifdef BIGOS_TCP_PATH_SMOKE' in tcp
    assert 'BIGOS_TCP_PATH_PASSED' in tcp
    assert 'BIGOS_TCP_PATH_FAILED' in tcp

    assert 'create_kernel_thread(&bigos::net::tcp_path_smoke_entry' in kernel


def test_stream_socket_change_supersedes_prior_no_socket_abi_non_goal() -> None:
    syscall_h = read_source('include/bigos/syscall.h')
    # The original bounded TCP protocol path exposed no user socket ABI. The later
    # bounded stream-socket-interface change deliberately adds the minimal TCP
    # socket ABI while still avoiding a broad SYS_TCP/socketcall surface.
    assert 'SYS_TCP' not in syscall_h
    assert 'SYS_CONNECT = 62' in syscall_h
    assert 'SYS_LISTEN = 63' in syscall_h
    assert 'SYS_ACCEPT = 64' in syscall_h
    assert 'SYS_GETSOCKOPT = 65' in syscall_h
    assert 'SYS_SEND = 66' in syscall_h
    assert 'SOCKET_SOCK_STREAM = 1' in syscall_h
    assert 'SOCKET_IPPROTO_TCP = 6' in syscall_h
