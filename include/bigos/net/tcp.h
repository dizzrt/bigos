#ifndef _BIGOS_NET_TCP_H
#define _BIGOS_NET_TCP_H

#include <bigos/net.h>
#include <bigos/timer.h>
#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace net {
    // Bounded kernel-internal TCP protocol path. This is the protocol-layer TCP
    // state machine only: it drives connection establishment, RFC 6298 dynamic
    // retransmission with a bounded window, in-order delivery with a bounded
    // reorder window, and connection teardown with a standard 2*MSL TIME_WAIT. It
    // does NOT implement the full TCP feature matrix (no congestion control, SACK,
    // window scaling, timestamps/PAWS, urgent pointer, or keepalive) and exposes
    // no new user-visible syscall/fd/socket ABI. TCP segments travel over the
    // existing send_ipv4 output layer and arrive through the existing handle_ipv4
    // input dispatch; local-address segments reuse the existing loopback split.

    enum class TcpState : uint8_t {
        Closed,
        Listen,
        SynSent,
        SynReceived,
        Established,
        FinWait1,
        FinWait2,
        Closing,
        CloseWait,
        LastAck,
        TimeWait,
    };

    // One entry in the bounded retransmit queue. Each holds a copy of the
    // (re)transmitted segment so it can be retransmitted without re-reading the
    // send buffer. Karn's algorithm: once retransmitted is set, the segment is
    // excluded from RTT sampling to avoid retransmission ambiguity.
    struct TcpRetxSegment {
        bool in_use;
        uint32_t seq;              // sequence of the first byte / control flag
        uint16_t data_len;         // TCP data bytes carried
        uint8_t flags;             // TCP control flags carried (SYN/FIN/ACK/PSH)
        bool retransmitted;        // Karn: excluded from RTT sampling when true
        uint32_t retransmit_count;
        timer::tick_t send_tick;           // original send tick (RTT sample basis)
        timer::tick_t rto_deadline_tick;   // next retransmit deadline
        uint8_t data[TCP_MSS];
    };

    // One bounded out-of-order reorder slot. Segments that fall inside the receive
    // window but ahead of rcv_nxt are cached here until the gap is filled.
    struct TcpReorderSlot {
        bool in_use;
        uint32_t seq;
        uint16_t len;
        uint8_t data[TCP_MSS];
    };

    struct TcpControlBlock {
        bool active;
        Context *owner;            // owning protocol context (bounds cross-context lookup)
        TcpState state;

        // Connection four-tuple. LISTEN TCBs match by local port only.
        Ipv4Address local_ip;
        uint16_t local_port;
        Ipv4Address remote_ip;
        uint16_t remote_port;

        // Send side. snd_una..snd_nxt is sent-but-unacked; snd_wnd is the peer's
        // advertised window (bounded).
        uint32_t snd_una;
        uint32_t snd_nxt;
        uint32_t snd_wnd;
        uint8_t send_buffer[TCP_SEND_BUFFER];   // bounded not-yet-segmented data
        uint32_t send_head;                     // ring read index
        uint32_t send_count;                    // buffered bytes pending segmentation
        TcpRetxSegment retx[TCP_RETX_QUEUE_CAPACITY];

        // RFC 6298 estimator state, all in monotonic ticks (integers, no float).
        bool rtt_valid;            // true once a first RTT sample exists
        uint32_t srtt;             // smoothed round-trip time (ticks)
        uint32_t rttvar;           // round-trip time variance (ticks)
        uint32_t rto;              // current retransmission timeout (ticks)

        // Receive side. Bounded receive buffer plus bounded reorder window.
        uint32_t rcv_nxt;
        uint32_t rcv_wnd;
        uint8_t recv_buffer[TCP_RECV_BUFFER];   // bounded in-order delivered bytes
        uint32_t recv_head;                     // ring read index
        uint32_t recv_count;                    // in-order bytes available to read
        TcpReorderSlot reorder[TCP_REORDER_SLOTS];

        bool fin_sent;             // a FIN occupies one sequence number once sent
        bool fin_received;         // peer FIN consumed one sequence number
        timer::tick_t time_wait_deadline_tick;
    };

    // Active open: allocate a TCB, send SYN, and enter SynSent. Completes to
    // Established when a matching SYN,ACK arrives (driven by handle_tcp). Ordinary
    // (non-IRQ) context only.
    Status tcp_open(Context *__ctx, Ipv4Address __local_ip, uint16_t __local_port, Ipv4Address __remote_ip,
                    uint16_t __remote_port, TcpControlBlock **__out) noexcept;

    // Passive open: allocate a TCB in Listen bound to a local port. A matching SYN
    // drives it through SynReceived to Established (see handle_tcp).
    Status tcp_listen(Context *__ctx, Ipv4Address __local_ip, uint16_t __local_port,
                      TcpControlBlock **__out) noexcept;

    // Queue and transmit up to __len bytes of stream data on an Established
    // connection, bounded by the send buffer, the advertised window, and the
    // retransmit queue. *__sent reports accepted bytes. Ordinary context only.
    Status tcp_send(Context *__ctx, TcpControlBlock *__tcb, const uint8_t *__data, uint32_t __len,
                    uint32_t *__sent) noexcept;

    // Read up to __cap bytes of in-order delivered stream data into __out.
    // *__got reports copied bytes; NoData when nothing is available.
    Status tcp_receive(TcpControlBlock *__tcb, uint8_t *__out, uint32_t __cap, uint32_t *__got) noexcept;

    // Begin an active close (send FIN, enter FinWait1) or complete a passive close
    // (CloseWait -> LastAck). Ordinary context only.
    Status tcp_close(Context *__ctx, TcpControlBlock *__tcb) noexcept;

    // IPv4 input entry for protocol number 6. Reads the TCP segment within the
    // validated IPv4 payload bounds, verifies the pseudo-header checksum, matches a
    // connection by four-tuple (or a LISTEN by local port), and drives the state
    // machine. Runs in ordinary context via the input dispatch.
    Status handle_tcp(Context *__ctx, Ipv4Address __source, Ipv4Address __dest, const uint8_t *__segment,
                      uint16_t __length) noexcept;

    // Ordinary-context timeout/retransmit advance (the ARP-pending style tick
    // driver). Retransmits expired unacked segments with exponential backoff and
    // recycles TIME_WAIT TCBs whose 2*MSL deadline has expired. MUST NOT run in
    // IRQ context.
    Status tcp_pump(Context *__ctx) noexcept;

    // Clear the entire bounded TCB pool (recycle all connections). Kernel-internal
    // reset used by validation setup; not a user-visible interface.
    void tcp_reset_state() noexcept;

#ifdef BIGOS_TCP_PATH_SMOKE
    // Default-off kernel-internal bounded TCP local-address closed-loop smoke. It
    // initializes a LoopbackReady context (local config, no frame-level device) and
    // exercises the three-way handshake (active + passive), Established in-order
    // bidirectional data delivery, out-of-order/duplicate handling, the RFC 6298
    // retransmit path, connection teardown with a standard 2*MSL TIME_WAIT, table
    // full, and RST/reset paths, emitting a deterministic BIGOS_TCP_PATH_PASSED /
    // BIGOS_TCP_PATH_FAILED marker without any real tap/network backend.
    void tcp_path_smoke_entry(void *) noexcept;
#endif
}   // namespace net
NAMESPACE_BIGOS_END

#endif   // _BIGOS_NET_TCP_H
