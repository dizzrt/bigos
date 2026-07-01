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

The protocol module does not expose sockets, fd objects, syscalls, `/dev`
nodes, libc socket calls, DHCP, DNS, TCP, IPv6, IP fragment reassembly, NAT,
firewalling, dynamic routing, or multi-interface routing. The minimal
user-visible UDP socket interface wraps this kernel-internal API in a separate
change (see `docs/en/arch/syscall-entry.md`); it stays a bounded UDP adapter and
does not change the protocol module's bounded capacities or non-goals.

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
