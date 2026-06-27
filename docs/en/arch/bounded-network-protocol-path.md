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
