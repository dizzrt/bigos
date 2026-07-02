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

        // Passive-open association (Linux/BSD dual-queue model). Appended fields;
        // the existing layout above is unchanged. A listener stays in Listen and
        // tracks derived child connections in two bounded queues of child TCB
        // pointers (both bounded by the constants in net.h); a child records the
        // listener it belongs to so teardown can unlink it. syn_queue holds
        // SynReceived children mid-handshake (half-open); accept_queue holds
        // Established children waiting for tcp_accept (full). All queue mutation and
        // wakeups run in ordinary (non-IRQ) context.
        bool is_child;                     // true for a derived passive-open connection
        TcpControlBlock *listener;         // owning listener (child only), else null
        TcpControlBlock *syn_queue[STREAM_SYN_QUEUE_CAPACITY];
        uint32_t syn_queue_count;
        TcpControlBlock *accept_queue[STREAM_ACCEPT_QUEUE_CAPACITY];
        uint32_t accept_queue_count;

        // Connection-level readiness wait queue. Woken when in-order data arrives,
        // the connection reaches Established, a listener gains an acceptable
        // connection, or the connection is reset. Contributed to fd readiness
        // (poll_wait) and the bounded stream socket blocking paths.
        sched::WaitQueue wait;

        // Monotonic slot generation, bumped every time the slot is allocated. A
        // stream socket records the generation of its TCB at association time so it
        // can detect that the slot was recycled/reset and possibly reused by a
        // different connection (tcp_connection_alive), avoiding a dangling TCB read.
        uint32_t generation;
    };

    // Layout guard: the passive-open association fields stay appended after the
    // historical TCB layout so existing offsets are unchanged.
    static_assert(__builtin_offsetof(TcpControlBlock, is_child) > __builtin_offsetof(TcpControlBlock, time_wait_deadline_tick),
                  "TCB passive-open fields must be appended after the historical layout");

    // Active open: allocate a TCB, send SYN, and enter SynSent. Completes to
    // Established when a matching SYN,ACK arrives (driven by handle_tcp). Ordinary
    // (non-IRQ) context only.
    Status tcp_open(Context *__ctx, Ipv4Address __local_ip, uint16_t __local_port, Ipv4Address __remote_ip,
                    uint16_t __remote_port, TcpControlBlock **__out) noexcept;

    // Passive open: allocate a TCB in Listen bound to a local port. A matching SYN
    // derives a child TCB (registered on the listener's half-open queue) that is
    // driven through SynReceived to Established and moved onto the accept queue;
    // the listener itself stays in Listen (see handle_tcp).
    Status tcp_listen(Context *__ctx, Ipv4Address __local_ip, uint16_t __local_port,
                      TcpControlBlock **__out) noexcept;

    // Take one Established child connection from a listener's full (accept) queue.
    // On success *__out is the child TCB (removed from the accept queue) and
    // Status::Ok is returned; when the accept queue is empty *__out is null and
    // Status::NoData is returned. Ordinary (non-IRQ) context only; it never blocks.
    Status tcp_accept(Context *__ctx, TcpControlBlock *__listener, TcpControlBlock **__out) noexcept;

    // Current allocation generation of the slot a TCB lives in. A caller (stream
    // socket backend) records this at association time and pairs it with
    // tcp_connection_alive() to detect that the slot was recycled/reused.
    uint32_t tcp_slot_generation(const TcpControlBlock *__tcb) noexcept;

    // True when __tcb is still the same active connection whose generation was
    // recorded as __generation (i.e. the slot was not recycled/reused). A null
    // pointer or a recycled/reused slot returns false. Read-only, no blocking.
    bool tcp_connection_alive(const TcpControlBlock *__tcb, uint32_t __generation) noexcept;

    // Read-only access to a connection's readiness wait queue, for fd readiness
    // (poll_wait) contribution. Returns null for a null TCB.
    sched::WaitQueue *tcp_wait_queue(TcpControlBlock *__tcb) noexcept;

    // Read-only connection-state queries for the stream socket backend. All are
    // O(1), no blocking, and tolerate a null TCB (returning the safe "false").
    //   tcp_is_established:  state == Established (writable/data-capable)
    //   tcp_peer_closed:     peer FIN consumed and no in-order data left -> read EOF
    //   tcp_write_closed:    local FIN already sent (write direction closed)
    //   tcp_send_room:       send path has room (window + a free retransmit slot)
    //   tcp_accept_ready:    listener has at least one completed connection to accept
    bool tcp_is_established(const TcpControlBlock *__tcb) noexcept;
    bool tcp_peer_closed(const TcpControlBlock *__tcb) noexcept;
    bool tcp_write_closed(const TcpControlBlock *__tcb) noexcept;
    bool tcp_send_room(const TcpControlBlock *__tcb) noexcept;
    bool tcp_accept_ready(const TcpControlBlock *__listener) noexcept;

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

    // Abort/recycle a connection immediately: wake any readiness waiters and free
    // the TCB slot (a still-queued child unlinks from its listener; a listener
    // recycles its queued children). Used by the stream socket close/rollback path
    // for a LISTEN or not-yet-Established connection, and to discard an accepted
    // connection on an accept fd-install failure. Ordinary context only.
    Status tcp_abort(Context *__ctx, TcpControlBlock *__tcb) noexcept;

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
