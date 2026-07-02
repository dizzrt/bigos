# Bounded Network Protocol Path

BigOS provides a kernel-internal bounded network protocol path above the
frame-level `NetworkDevice` interface. The path handles Ethernet II dispatch,
bounded ARP cache and pending resolution, unfragmented IPv4 validation, ICMPv4
echo reply, and a kernel-only UDP datagram endpoint table.

The protocol context is single-interface only. Initialization requires an
already-ready frame-level network device plus static IPv4 configuration
supplied by the kernel caller. Missing devices or invalid configuration leave
the context disabled with deterministic diagnostics; default boot, storage,
filesystem, `/rw`, shell, and userland do not depend on network availability.

The protocol module remains bounded and single-context: it does not expose
`/dev` network nodes, DHCP, DNS, IPv6, IP fragment reassembly, NAT, firewalling,
dynamic routing, or multi-interface routing. User-visible sockets are explicit
bounded adapters over this protocol path: the existing UDP datagram interface and
the minimal TCP stream socket interface are documented in
`docs/en/arch/syscall-entry.md`. They do not change the protocol module's
bounded capacities or broader non-goals.

Protocol parsing runs only in ordinary kernel context. Virtio-net MSI-X handlers
remain limited to frame-level RX/TX completion state; the protocol pump and UDP
endpoint operations reject IRQ/nonblocking contexts. RX buffers are returned
exactly once after frame processing, while TX success is reported only from the
underlying network-device transmit result.

Validation is default-off through the bounded network protocol smoke case. It
checks initialization, ARP request/reply state, IPv4 validation, ICMP echo,
UDP bind/send/receive, and unsupported-frame rejection without making the normal
boot path depend on a host network backend.

## Local-Address Loopback Path

The IPv4 output layer classifies the destination and gives local-address traffic
a kernel-internal loopback path. The local-address set is the configured local
IPv4 address plus the whole loopback network `127.0.0.0/8` (not only
`127.0.0.1`). When `send_ipv4` builds a packet for a local-address destination,
it delivers the packet directly to the IPv4 input dispatch instead of resolving
ARP or transmitting through the frame-level device. Every other destination
keeps the existing `route_destination` + ARP + frame-level device path unchanged.

Local delivery normalizes the source/destination domain onto `local_ipv4`
(`127.0.0.1` is treated as an alias of `local_ipv4` entering the input path), so
the existing UDP pseudo-header and IPv4 checksum logic stays self-consistent with
no special case. Local UDP datagrams reuse the same bounded UDP endpoint RX queue
and readiness wakeups as inbound frames; queue-full and unbound-port results are
the same deterministic states. ICMP echo-to-self is bounded: an echo request
produces exactly one echo reply, which re-enters the input path, is recognized as
a reply, and generates no new request (recursion depth is at most one echo).

The local-address path does not require a ready frame-level device. When a
context has valid local IPv4 configuration but no ready `NetworkDevice`, it enters
a loopback-only readiness mode: local-address bind/send/receive work, while
outbound (non-local) sends still return a deterministic not-ready/device status.
An unconfigured default boot never enters this mode, so default boot, storage,
filesystem, `/rw`, shell, and userland stay independent of the network path.

Two append-only diagnostics counters, `loopback_delivered` and
`loopback_dropped`, distinguish a local-loopback hit from an outbound
frame-level hit without changing existing counters. The loopback path adds no new
user-visible syscall/fd/socket ABI, no `lo` device node, no interface
enumeration, and no general routing/forwarding or multi-interface model; user
programs benefit only by using the existing UDP socket against a local address.
Validation is default-off through the `loopback_network_smoke` build switch,
which emits `BIGOS_LOOPBACK_NETWORK_PASSED` / `BIGOS_LOOPBACK_NETWORK_FAILED` on
COM1 and covers the UDP loopback closed loop, the ICMP echo-to-self, and the
unbound/queue-full/non-local error paths without a real tap or network card.

## Bounded TCP Path

The protocol path carries a kernel-internal bounded TCP state machine on top of
the same IPv4 layer. IPv4 input dispatch demultiplexes protocol number 6 (TCP)
to `handle_tcp`, and TCP segments are emitted through the existing `send_ipv4`
output layer, so local-address TCP segments reuse the loopback split and
outbound segments reuse `route_destination` + ARP + the frame-level device with
no second IPv4 output or checksum path. The TCP checksum covers the IPv4
pseudo-header (source/destination IPv4, protocol 6, TCP length) plus the TCP
header and data; for the local-address closed loop the pseudo-header destination
is normalized onto `local_ipv4` exactly like the IPv4 header, so `handle_tcp`
rebuilds a self-consistent checksum with no special case.

TCP state lives in a compile-time-bounded control-block (TCB) pool. Each TCB
holds the connection four-tuple, the `TcpState` (Closed/Listen/SynSent/
SynReceived/Established/FinWait1/FinWait2/Closing/CloseWait/LastAck/TimeWait),
bounded send/receive buffers, a bounded retransmit queue, and a bounded
out-of-order reorder window. Connections are matched by exact four-tuple.
Passive open uses a Linux/BSD-style listener plus child TCB model: a `LISTEN`
TCB stays in `Listen`, matching by local port; inbound SYNs derive child TCBs in
`SynReceived` on a bounded SYN queue, and final ACKs move established children
onto the listener's bounded accept queue. Sequence comparisons use 32-bit
wraparound-safe signed-difference arithmetic. The pool, buffers, retransmit
queue, reorder window, SYN queue, and accept queue never grow past their
compile-time bounds: a full pool or full queue deterministically drops/refuses
the excess connection and counts a drop.

Retransmission follows RFC 6298 with integer/shift math only (no floating
point): the estimator maintains `SRTT`/`RTTVAR` (alpha=1/8, beta=1/4) and
computes `RTO = SRTT + max(G, 4*RTTVAR)` clamped to `[TCP_RTO_MIN, TCP_RTO_MAX]`
(aligned to Linux 200ms / 120s). RTT sampling obeys Karn's algorithm
(retransmitted segments are excluded), timeouts retransmit with exponential
backoff (`RTO = min(RTO*2, TCP_RTO_MAX)`) without rewriting `SRTT`/`RTTVAR`, and
exceeding the bounded retransmit limit deterministically resets the connection.
Flow control advertises the bounded receive-buffer free space; a full buffer
advertises a bounded (including zero) window and does not buffer unboundedly.
Data delivery is sequence-driven: in-order data advances `rcv_nxt`, in-window
out-of-order segments are buffered in the bounded reorder window and merged when
the gap fills, and duplicate/out-of-window/checksum-failed segments are dropped
deterministically without corrupting delivered data. Retransmission, RTT
sampling, and `TIME_WAIT` recycling run only in ordinary (non-IRQ) context,
reusing the ARP-pending style tick-driven advance; they never allocate, block,
or touch TCB buffers from an IRQ context.

Teardown covers active close (FIN -> FinWait1 -> FinWait2/TimeWait), passive
close (CloseWait -> LastAck), and simultaneous close (Closing). `TIME_WAIT` uses
the standard `2*MSL` (`2*TCP_MSL_TICKS`) before the TCB is recycled; when the
bounded pool has a `TIME_WAIT` slot occupied, a new connection returns the
deterministic table-full status rather than reclaiming the slot early. A matched
RST, a retransmit-limit overflow, or an unrecoverable illegal segment resets the
connection and recycles the TCB without leaking its bounded buffers. Append-only
TCP diagnostics counters (`tcp_segments_rx`/`tcp_segments_tx`/`tcp_retransmits`/
`tcp_connections_opened`/`tcp_connections_closed`/`tcp_resets`/`tcp_dropped`)
sit after the loopback counters and before `last_status`, guarded by
`static_assert` offset checks, without changing existing counter semantics.

This capability is the first TCP compatibility step: it implements connection
setup, RFC 6298 dynamic retransmission, in-order delivery/reassembly, and
teardown. Congestion control, SACK, window scaling, timestamps/PAWS, urgent
pointer, keepalive, and broader POSIX socket behavior remain staged expansion
work rather than permanent non-goals. The current user-visible TCP stream socket
adapter exposes the syscall/fd surface documented in
`docs/en/arch/syscall-entry.md`; name resolution and broader socket behavior are
handled by later resolver/socket compatibility work. Validation is default-off through the `tcp_path_smoke`
build switch, which emits `BIGOS_TCP_PATH_PASSED` / `BIGOS_TCP_PATH_FAILED` on
COM1 and covers the local-address three-way handshake (active + passive),
Established in-order bidirectional data delivery, bounded reorder and duplicate
drop, the RFC 6298 retransmit path with backoff and Karn sampling, reset on the
retransmit limit, teardown with the standard `2*MSL` `TIME_WAIT` recycle, the
connection-table-full path, and an illegal segment — all over the loopback-ready
protocol path with no real tap or network card. Default boot never initializes
TCP and stays independent of this capability.
