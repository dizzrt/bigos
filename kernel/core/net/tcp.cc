#include <bigos/net/tcp.h>

#include <bigos/io.h>
#include <bigos/net.h>
#include <bigos/sched.h>
#include <bigos/timer.h>

#include <string.h>

namespace {
    // TCP control flags.
    constexpr uint8_t TCP_FIN = 0x01;
    constexpr uint8_t TCP_SYN = 0x02;
    constexpr uint8_t TCP_RST = 0x04;
    constexpr uint8_t TCP_PSH = 0x08;
    constexpr uint8_t TCP_ACK = 0x10;

    constexpr uint16_t TCP_HEADER_LEN = 20;
    constexpr uint32_t G_TICK = 1;   // RFC 6298 clock granularity, 1 tick

    // Bounded, statically-owned TCB pool. Kernel-thread stacks are one page, so
    // TCBs (which hold the bounded send/receive/retransmit/reorder storage) live
    // here rather than on the stack. All TCP protocol work runs in ordinary
    // (non-IRQ) context, so no locking is needed under the single-core cooperative
    // model this validation path targets.
    bigos::net::TcpControlBlock g_tcbs[bigos::net::TCP_CONNECTION_CAPACITY] = {};

    // File-scope scratch for building an outgoing TCP segment. Safe under the
    // synchronous loopback re-entry: each builder fully fills the scratch and hands
    // it to ipv4_send (which copies the payload into its own buffer) before any
    // nested handle_tcp runs, and never reads the scratch after that call.
    uint8_t g_tx_seg[TCP_HEADER_LEN + bigos::net::TCP_MSS] = {};

    uint32_t g_isn_counter = 1;

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

    uint16_t ones_complement(const uint8_t *__data, uint32_t __length, uint32_t __sum) noexcept {
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

    // TCP checksum over the IPv4 pseudo-header (source, dest, zero, protocol 6, TCP
    // length) followed by the TCP header and data. For a local-address closed loop
    // the source/dest domain is normalized onto local_ipv4 by send_ipv4, so the
    // rebuilt checksum here is self-consistent without any special case.
    uint16_t tcp_checksum(bigos::net::Ipv4Address __src, bigos::net::Ipv4Address __dst, const uint8_t *__segment,
        uint16_t __length) noexcept {
        uint8_t pseudo[12] = {};
        write_be32(pseudo + 0, __src.value);
        write_be32(pseudo + 4, __dst.value);
        pseudo[8] = 0;
        pseudo[9] = bigos::net::IPV4_PROTOCOL_TCP;
        write_be16(pseudo + 10, __length);
        uint32_t sum = 0;
        // Fold the pseudo-header first, then continue over the segment. Passing the
        // running sum through both calls keeps the 16-bit alignment correct.
        {
            uint32_t i = 0;
            while (i + 1 < sizeof(pseudo)) {
                sum += read_be16(pseudo + i);
                i += 2;
            }
        }
        return ones_complement(__segment, __length, sum);
    }

    // 32-bit sequence comparison using signed difference. This is the non-obvious
    // wraparound-safe rule: (int32_t)(a - b) < 0 means a is "before" b in modulo-2^32
    // sequence space, so comparisons stay correct across the 2^32 wrap boundary
    // instead of misjudging a valid segment as out-of-range.
    bool seq_lt(uint32_t __a, uint32_t __b) noexcept {
        return (int32_t)(__a - __b) < 0;
    }
    bool seq_leq(uint32_t __a, uint32_t __b) noexcept {
        return (int32_t)(__a - __b) <= 0;
    }
    bool seq_gt(uint32_t __a, uint32_t __b) noexcept {
        return (int32_t)(__a - __b) > 0;
    }

    bool ordinary_context() noexcept {
        const bigos::cpu::LocalState &local = bigos::cpu::current_state();
        return local.irq_nesting_depth == 0 && local.nonblocking_depth == 0;
    }

    uint32_t clamp_rto(uint32_t __rto) noexcept {
        if (__rto < bigos::net::TCP_RTO_MIN)
            return bigos::net::TCP_RTO_MIN;
        if (__rto > bigos::net::TCP_RTO_MAX)
            return bigos::net::TCP_RTO_MAX;
        return __rto;
    }

    // Recycle a TCB: release its bounded buffers/reorder slots by zeroing the whole
    // block so the slot can be reused deterministically.
    void recycle_tcb(bigos::net::TcpControlBlock *__tcb) noexcept {
        if (__tcb != nullptr)
            *__tcb = {};
    }

    bigos::net::TcpControlBlock *find_connection(bigos::net::Context *__ctx, bigos::net::Ipv4Address __local_ip,
        uint16_t __local_port, bigos::net::Ipv4Address __remote_ip, uint16_t __remote_port) noexcept {
        for (uint32_t i = 0; i < bigos::net::TCP_CONNECTION_CAPACITY; i++) {
            bigos::net::TcpControlBlock &tcb = g_tcbs[i];
            if (!tcb.active || tcb.owner != __ctx || tcb.state == bigos::net::TcpState::Listen)
                continue;
            if (tcb.local_port == __local_port && tcb.remote_port == __remote_port &&
                tcb.local_ip.value == __local_ip.value && tcb.remote_ip.value == __remote_ip.value)
                return &tcb;
        }
        return nullptr;
    }

    bigos::net::TcpControlBlock *find_listener(bigos::net::Context *__ctx, uint16_t __local_port) noexcept {
        for (uint32_t i = 0; i < bigos::net::TCP_CONNECTION_CAPACITY; i++) {
            bigos::net::TcpControlBlock &tcb = g_tcbs[i];
            if (tcb.active && tcb.owner == __ctx && tcb.state == bigos::net::TcpState::Listen &&
                tcb.local_port == __local_port)
                return &tcb;
        }
        return nullptr;
    }

    // Allocate a free TCB slot. No free slot is a deterministic TableFull; the
    // caller counts tcp_dropped. Never overwrites an active connection (including
    // TIME_WAIT connections still occupying a slot).
    bigos::net::TcpControlBlock *alloc_tcb(bigos::net::Context *__ctx) noexcept {
        for (uint32_t i = 0; i < bigos::net::TCP_CONNECTION_CAPACITY; i++) {
            if (!g_tcbs[i].active) {
                recycle_tcb(&g_tcbs[i]);
                g_tcbs[i].active = true;
                g_tcbs[i].owner = __ctx;
                return &g_tcbs[i];
            }
        }
        return nullptr;
    }

    uint16_t advertised_window(const bigos::net::TcpControlBlock *__tcb) noexcept {
        uint32_t used = __tcb->recv_count;
        for (uint32_t i = 0; i < bigos::net::TCP_REORDER_SLOTS; i++) {
            if (__tcb->reorder[i].in_use)
                used += __tcb->reorder[i].len;
        }
        uint32_t free = used >= bigos::net::TCP_RECV_BUFFER ? 0 : bigos::net::TCP_RECV_BUFFER - used;
        if (free > 0xffffu)
            free = 0xffffu;
        return (uint16_t)free;
    }

    // Build the fixed 20-byte TCP header plus optional data into the scratch buffer
    // and hand it to the shared IPv4 output layer. The window field carries the
    // bounded advertised receive window for flow control.
    bigos::net::Status transmit_segment(bigos::net::Context *__ctx, bigos::net::TcpControlBlock *__tcb, uint32_t __seq,
        uint32_t __ack, uint8_t __flags, const uint8_t *__data, uint16_t __data_len) noexcept {
        if (__data_len > bigos::net::TCP_MSS)
            __data_len = bigos::net::TCP_MSS;
        uint8_t *seg = g_tx_seg;
        write_be16(seg + 0, __tcb->local_port);
        write_be16(seg + 2, __tcb->remote_port);
        write_be32(seg + 4, __seq);
        write_be32(seg + 8, __ack);
        seg[12] = (uint8_t)((TCP_HEADER_LEN / 4) << 4);
        seg[13] = __flags;
        write_be16(seg + 14, advertised_window(__tcb));
        write_be16(seg + 16, 0);
        write_be16(seg + 18, 0);
        if (__data_len != 0 && __data != nullptr)
            memcpy(seg + TCP_HEADER_LEN, __data, __data_len);
        const uint16_t seg_len = (uint16_t)(TCP_HEADER_LEN + __data_len);
        // Checksum against the same IPv4 header addresses send_ipv4 writes: the
        // source is always local_ipv4, and a local-address destination is
        // normalized onto local_ipv4 (the loopback split). handle_tcp rebuilds the
        // checksum from those IP-header addresses, so this stays self-consistent
        // with no special case; outbound destinations use the real remote address.
        const bigos::net::Ipv4Address csum_src = __ctx->config.local_ipv4;
        const bool local =
            (__tcb->remote_ip.value == __ctx->config.local_ipv4.value) ||
            ((__tcb->remote_ip.value & bigos::net::IPV4_LOOPBACK_MASK) == bigos::net::IPV4_LOOPBACK_PREFIX);
        const bigos::net::Ipv4Address csum_dst = local ? __ctx->config.local_ipv4 : __tcb->remote_ip;
        const uint16_t checksum = tcp_checksum(csum_src, csum_dst, seg, seg_len);
        write_be16(seg + 16, checksum);
        const bigos::net::Status status =
            bigos::net::ipv4_send(__ctx, __tcb->remote_ip, bigos::net::IPV4_PROTOCOL_TCP, seg, seg_len);
        if (status == bigos::net::Status::Ok)
            __ctx->diagnostics.tcp_segments_tx++;
        return status;
    }

    // Register a segment that consumes sequence space into the bounded retransmit
    // queue so it can be retransmitted and RTT-sampled. Returns false when the
    // bounded queue is full (deterministic QueueFull to the caller).
    bool register_retx(bigos::net::TcpControlBlock *__tcb, uint32_t __seq, uint8_t __flags, const uint8_t *__data,
        uint16_t __data_len) noexcept {
        for (uint32_t i = 0; i < bigos::net::TCP_RETX_QUEUE_CAPACITY; i++) {
            bigos::net::TcpRetxSegment &e = __tcb->retx[i];
            if (e.in_use)
                continue;
            e.in_use = true;
            e.seq = __seq;
            e.flags = __flags;
            e.data_len = __data_len;
            e.retransmitted = false;
            e.retransmit_count = 0;
            e.send_tick = bigos::timer::ticks();
            e.rto_deadline_tick = e.send_tick + __tcb->rto;
            if (__data_len != 0 && __data != nullptr)
                memcpy(e.data, __data, __data_len);
            return true;
        }
        return false;
    }

    uint16_t retx_seq_span(const bigos::net::TcpRetxSegment &__e) noexcept {
        uint16_t span = __e.data_len;
        if ((__e.flags & TCP_SYN) != 0)
            span++;
        if ((__e.flags & TCP_FIN) != 0)
            span++;
        return span;
    }

    // Emit a new segment that consumes sequence space (SYN / FIN / data), assigning
    // seq = snd_nxt, registering it for retransmission, then advancing snd_nxt.
    bigos::net::Status emit_new(bigos::net::Context *__ctx, bigos::net::TcpControlBlock *__tcb, uint8_t __flags,
        const uint8_t *__data, uint16_t __data_len) noexcept {
        const uint32_t seq = __tcb->snd_nxt;
        uint16_t span = __data_len;
        if ((__flags & TCP_SYN) != 0)
            span++;
        if ((__flags & TCP_FIN) != 0)
            span++;
        if (span != 0 && !register_retx(__tcb, seq, __flags, __data, __data_len))
            return bigos::net::Status::QueueFull;
        __tcb->snd_nxt += span;
        const uint32_t ack = (__flags & TCP_ACK) != 0 ? __tcb->rcv_nxt : 0;
        return transmit_segment(__ctx, __tcb, seq, ack, __flags, __data, __data_len);
    }

    // Emit a pure ACK (no sequence space consumed, not retransmitted).
    bigos::net::Status emit_ack(bigos::net::Context *__ctx, bigos::net::TcpControlBlock *__tcb) noexcept {
        return transmit_segment(__ctx, __tcb, __tcb->snd_nxt, __tcb->rcv_nxt, TCP_ACK, nullptr, 0);
    }

    // RFC 6298 estimator update on a fresh RTT sample R (ticks). Integer/shift math
    // only: alpha = 1/8 (>>3), beta = 1/4 (>>2). rto = srtt + max(G, 4*rttvar),
    // clamped to [TCP_RTO_MIN, TCP_RTO_MAX].
    void rtt_update(bigos::net::TcpControlBlock *__tcb, uint32_t __r) noexcept {
        if (!__tcb->rtt_valid) {
            __tcb->srtt = __r;
            __tcb->rttvar = __r >> 1;
            __tcb->rtt_valid = true;
        } else {
            const uint32_t err = __tcb->srtt > __r ? __tcb->srtt - __r : __r - __tcb->srtt;
            __tcb->rttvar = __tcb->rttvar - (__tcb->rttvar >> 2) + (err >> 2);
            __tcb->srtt = __tcb->srtt - (__tcb->srtt >> 3) + (__r >> 3);
        }
        const uint32_t four_rttvar = __tcb->rttvar << 2;
        const uint32_t var_term = four_rttvar > G_TICK ? four_rttvar : G_TICK;
        __tcb->rto = clamp_rto(__tcb->srtt + var_term);
    }

    // Process a cumulative ACK: advance snd_una, recycle fully-acked retransmit
    // entries, sample RTT for entries never retransmitted (Karn's algorithm), and
    // update the peer's advertised window. Old/duplicate ACKs do not break the
    // send window.
    void process_ack(bigos::net::TcpControlBlock *__tcb, uint32_t __ack, uint16_t __window) noexcept {
        if (seq_gt(__ack, __tcb->snd_una) && seq_leq(__ack, __tcb->snd_nxt)) {
            const bigos::timer::tick_t now = bigos::timer::ticks();
            for (uint32_t i = 0; i < bigos::net::TCP_RETX_QUEUE_CAPACITY; i++) {
                bigos::net::TcpRetxSegment &e = __tcb->retx[i];
                if (!e.in_use)
                    continue;
                const uint32_t end = e.seq + retx_seq_span(e);
                if (seq_leq(end, __ack)) {
                    if (!e.retransmitted) {
                        const uint32_t r = (uint32_t)(now - e.send_tick);
                        rtt_update(__tcb, r);
                    }
                    e.in_use = false;
                }
            }
            __tcb->snd_una = __ack;
            __tcb->snd_wnd = __window;
        }
    }

    // Try to merge any buffered reorder slot that now starts exactly at rcv_nxt,
    // repeating until no slot lines up. Bounded by TCP_REORDER_SLOTS per pass.
    void drain_reorder(bigos::net::TcpControlBlock *__tcb) noexcept {
        bool merged = true;
        while (merged) {
            merged = false;
            for (uint32_t i = 0; i < bigos::net::TCP_REORDER_SLOTS; i++) {
                bigos::net::TcpReorderSlot &slot = __tcb->reorder[i];
                if (!slot.in_use || slot.seq != __tcb->rcv_nxt)
                    continue;
                uint32_t free = bigos::net::TCP_RECV_BUFFER - __tcb->recv_count;
                uint32_t take = slot.len < free ? slot.len : free;
                for (uint32_t b = 0; b < take; b++) {
                    const uint32_t idx = (__tcb->recv_head + __tcb->recv_count) % bigos::net::TCP_RECV_BUFFER;
                    __tcb->recv_buffer[idx] = slot.data[b];
                    __tcb->recv_count++;
                }
                __tcb->rcv_nxt += take;
                if (take == slot.len) {
                    slot.in_use = false;
                    merged = true;
                }
            }
        }
    }

    // Accept in-order/out-of-order data within the receive window. Returns true
    // when the segment carried acceptable data (so an ACK should follow). Duplicate,
    // out-of-window, or reorder-overflow data is dropped deterministically and
    // counted, without corrupting already-delivered bytes.
    bool accept_data(
        bigos::net::TcpControlBlock *__tcb, uint32_t __seq, const uint8_t *__data, uint16_t __len) noexcept {
        if (__len == 0)
            return false;
        const uint32_t wnd_end = __tcb->rcv_nxt + advertised_window(__tcb);
        if (__seq == __tcb->rcv_nxt) {
            uint32_t free = bigos::net::TCP_RECV_BUFFER - __tcb->recv_count;
            uint32_t take = __len < free ? __len : free;
            for (uint32_t b = 0; b < take; b++) {
                const uint32_t idx = (__tcb->recv_head + __tcb->recv_count) % bigos::net::TCP_RECV_BUFFER;
                __tcb->recv_buffer[idx] = __data[b];
                __tcb->recv_count++;
            }
            __tcb->rcv_nxt += take;
            drain_reorder(__tcb);
            if (take < __len)
                __tcb->owner->diagnostics.tcp_dropped++;
            return true;
        }
        if (seq_gt(__seq, __tcb->rcv_nxt) && seq_lt(__seq, wnd_end)) {
            for (uint32_t i = 0; i < bigos::net::TCP_REORDER_SLOTS; i++) {
                if (__tcb->reorder[i].in_use && __tcb->reorder[i].seq == __seq)
                    return true;   // already buffered; ACK current rcv_nxt
            }
            for (uint32_t i = 0; i < bigos::net::TCP_REORDER_SLOTS; i++) {
                bigos::net::TcpReorderSlot &slot = __tcb->reorder[i];
                if (slot.in_use)
                    continue;
                uint16_t take = __len < bigos::net::TCP_MSS ? __len : bigos::net::TCP_MSS;
                slot.in_use = true;
                slot.seq = __seq;
                slot.len = take;
                memcpy(slot.data, __data, take);
                return true;
            }
            __tcb->owner->diagnostics.tcp_dropped++;   // reorder window full
            return true;
        }
        // Duplicate (below rcv_nxt) or beyond the advertised window.
        __tcb->owner->diagnostics.tcp_dropped++;
        return false;
    }

    uint32_t next_isn() noexcept {
        const uint32_t isn = 0x10000u * g_isn_counter + (uint32_t)bigos::timer::ticks();
        g_isn_counter++;
        return isn;
    }

    // Reset a connection deterministically: recycle its slot and count the reset.
    void reset_connection(bigos::net::Context *__ctx, bigos::net::TcpControlBlock *__tcb) noexcept {
        __ctx->diagnostics.tcp_resets++;
        recycle_tcb(__tcb);
    }

    void enter_time_wait(bigos::net::TcpControlBlock *__tcb) noexcept {
        // Standard 2*MSL TIME_WAIT: hold the slot for two maximum segment lifetimes
        // before recycling so late duplicates cannot alias a new connection. The
        // slot stays occupied until tcp_pump recycles it at the deadline; it is not
        // reclaimed early for a new connection.
        __tcb->state = bigos::net::TcpState::TimeWait;
        __tcb->time_wait_deadline_tick = bigos::timer::ticks() + 2 * bigos::net::TCP_MSL_TICKS;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace net {
    void tcp_reset_state() noexcept {
        for (uint32_t i = 0; i < TCP_CONNECTION_CAPACITY; i++)
            g_tcbs[i] = {};
        g_isn_counter = 1;
    }

    Status tcp_open(Context *__ctx, Ipv4Address __local_ip, uint16_t __local_port, Ipv4Address __remote_ip,
        uint16_t __remote_port, TcpControlBlock **__out) noexcept {
        if (__ctx == nullptr || __out == nullptr || __local_port == 0 || __remote_port == 0)
            return Status::InvalidArgument;
        if (!ordinary_context())
            return Status::UnsupportedContext;
        *__out = nullptr;
        TcpControlBlock *tcb = alloc_tcb(__ctx);
        if (tcb == nullptr) {
            __ctx->diagnostics.tcp_dropped++;
            return Status::TableFull;
        }
        tcb->state = TcpState::SynSent;
        tcb->local_ip = __local_ip;
        tcb->local_port = __local_port;
        tcb->remote_ip = __remote_ip;
        tcb->remote_port = __remote_port;
        const uint32_t isn = next_isn();
        tcb->snd_una = isn;
        tcb->snd_nxt = isn;
        tcb->snd_wnd = TCP_RECV_BUFFER;
        tcb->rcv_wnd = TCP_RECV_BUFFER;
        tcb->rto = TCP_RTO_MIN;
        const Status status = emit_new(__ctx, tcb, TCP_SYN, nullptr, 0);
        if (status != Status::Ok) {
            recycle_tcb(tcb);
            return status;
        }
        *__out = tcb;
        return Status::Ok;
    }

    Status tcp_listen(Context *__ctx, Ipv4Address __local_ip, uint16_t __local_port, TcpControlBlock **__out) noexcept {
        if (__ctx == nullptr || __out == nullptr || __local_port == 0)
            return Status::InvalidArgument;
        if (!ordinary_context())
            return Status::UnsupportedContext;
        *__out = nullptr;
        TcpControlBlock *tcb = alloc_tcb(__ctx);
        if (tcb == nullptr) {
            __ctx->diagnostics.tcp_dropped++;
            return Status::TableFull;
        }
        tcb->state = TcpState::Listen;
        tcb->local_ip = __local_ip;
        tcb->local_port = __local_port;
        tcb->rcv_wnd = TCP_RECV_BUFFER;
        tcb->rto = TCP_RTO_MIN;
        *__out = tcb;
        return Status::Ok;
    }

    Status tcp_send(
        Context *__ctx, TcpControlBlock *__tcb, const uint8_t *__data, uint32_t __len, uint32_t *__sent) noexcept {
        if (__ctx == nullptr || __tcb == nullptr || (__data == nullptr && __len != 0))
            return Status::InvalidArgument;
        if (!ordinary_context())
            return Status::UnsupportedContext;
        if (__sent != nullptr)
            *__sent = 0;
        if (__tcb->state != TcpState::Established && __tcb->state != TcpState::CloseWait)
            return Status::NotReady;
        uint32_t sent = 0;
        while (sent < __len) {
            // Bounded by the peer's advertised window, the MSS, and free retransmit
            // slots. Never buffers unboundedly: stops when any bound is hit.
            const uint32_t inflight = __tcb->snd_nxt - __tcb->snd_una;
            if (inflight >= __tcb->snd_wnd)
                break;
            bool have_slot = false;
            for (uint32_t i = 0; i < TCP_RETX_QUEUE_CAPACITY; i++) {
                if (!__tcb->retx[i].in_use) {
                    have_slot = true;
                    break;
                }
            }
            if (!have_slot)
                break;
            uint32_t window_room = __tcb->snd_wnd - inflight;
            uint32_t chunk = __len - sent;
            if (chunk > TCP_MSS)
                chunk = TCP_MSS;
            if (chunk > window_room)
                chunk = window_room;
            if (chunk == 0)
                break;
            const Status status = emit_new(__ctx, __tcb, TCP_ACK | TCP_PSH, __data + sent, (uint16_t)chunk);
            if (status != Status::Ok) {
                if (__sent != nullptr)
                    *__sent = sent;
                return status;
            }
            sent += chunk;
        }
        if (__sent != nullptr)
            *__sent = sent;
        return Status::Ok;
    }

    Status tcp_receive(TcpControlBlock *__tcb, uint8_t *__out, uint32_t __cap, uint32_t *__got) noexcept {
        if (__tcb == nullptr || __out == nullptr)
            return Status::InvalidArgument;
        if (!ordinary_context())
            return Status::UnsupportedContext;
        if (__got != nullptr)
            *__got = 0;
        if (__tcb->recv_count == 0)
            return Status::NoData;
        uint32_t take = __tcb->recv_count < __cap ? __tcb->recv_count : __cap;
        for (uint32_t b = 0; b < take; b++) {
            __out[b] = __tcb->recv_buffer[__tcb->recv_head];
            __tcb->recv_head = (__tcb->recv_head + 1) % TCP_RECV_BUFFER;
        }
        __tcb->recv_count -= take;
        if (__got != nullptr)
            *__got = take;
        return Status::Ok;
    }

    Status tcp_close(Context *__ctx, TcpControlBlock *__tcb) noexcept {
        if (__ctx == nullptr || __tcb == nullptr)
            return Status::InvalidArgument;
        if (!ordinary_context())
            return Status::UnsupportedContext;
        // Set the next state and counters BEFORE emitting the FIN: under the
        // synchronous local-address loopback the peer's ACK is delivered (and this
        // TCB may even be recycled) inside emit_new, so nothing is mutated after.
        if (__tcb->state == TcpState::Established) {
            __tcb->fin_sent = true;
            __tcb->state = TcpState::FinWait1;
            __ctx->diagnostics.tcp_connections_closed++;
            return emit_new(__ctx, __tcb, TCP_ACK | TCP_FIN, nullptr, 0);
        }
        if (__tcb->state == TcpState::CloseWait) {
            __tcb->fin_sent = true;
            __tcb->state = TcpState::LastAck;
            __ctx->diagnostics.tcp_connections_closed++;
            return emit_new(__ctx, __tcb, TCP_ACK | TCP_FIN, nullptr, 0);
        }
        return Status::NotReady;
    }

    Status handle_tcp(Context *__ctx, Ipv4Address __source, Ipv4Address __dest, const uint8_t *__segment,
        uint16_t __length) noexcept {
        if (__ctx == nullptr || __segment == nullptr || __length < TCP_HEADER_LEN) {
            if (__ctx != nullptr)
                __ctx->diagnostics.tcp_dropped++;
            return Status::Malformed;
        }
        const uint8_t data_offset = (uint8_t)((__segment[12] >> 4) * 4);
        if (data_offset < TCP_HEADER_LEN || data_offset > __length) {
            __ctx->diagnostics.tcp_dropped++;
            return Status::Malformed;
        }
        if (tcp_checksum(__source, __dest, __segment, __length) != 0) {
            __ctx->diagnostics.tcp_dropped++;
            return Status::Malformed;
        }
        const uint16_t src_port = read_be16(__segment + 0);
        const uint16_t dst_port = read_be16(__segment + 2);
        const uint32_t seq = read_be32(__segment + 4);
        const uint32_t ack = read_be32(__segment + 8);
        const uint8_t flags = __segment[13];
        const uint16_t window = read_be16(__segment + 14);
        const uint8_t *data = __segment + data_offset;
        const uint16_t data_len = (uint16_t)(__length - data_offset);

        TcpControlBlock *tcb = find_connection(__ctx, __dest, dst_port, __source, src_port);
        if (tcb == nullptr) {
            // No exact connection. A SYN with no ACK can match a LISTEN; anything
            // else with no connection is dropped deterministically.
            if ((flags & TCP_SYN) != 0 && (flags & TCP_ACK) == 0) {
                TcpControlBlock *listener = find_listener(__ctx, dst_port);
                if (listener == nullptr) {
                    __ctx->diagnostics.tcp_dropped++;
                    return Status::NotBound;
                }
                // Passive open: bind the LISTEN TCB to this peer and reply SYN,ACK.
                listener->remote_ip = __source;
                listener->remote_port = src_port;
                listener->local_ip = __dest;
                listener->rcv_nxt = seq + 1;   // consume peer SYN
                const uint32_t isn = next_isn();
                listener->snd_una = isn;
                listener->snd_nxt = isn;
                listener->snd_wnd = window;
                listener->state = TcpState::SynReceived;
                const Status status = emit_new(__ctx, listener, TCP_SYN | TCP_ACK, nullptr, 0);
                if (status != Status::Ok) {
                    recycle_tcb(listener);
                    return status;
                }
                return Status::Ok;
            }
            __ctx->diagnostics.tcp_dropped++;
            return Status::NotBound;
        }

        // A matching RST resets the connection immediately from any state.
        if ((flags & TCP_RST) != 0) {
            reset_connection(__ctx, tcb);
            return Status::Ok;
        }

        switch (tcb->state) {
            case TcpState::SynSent: {
                // Expect SYN,ACK acknowledging our SYN.
                if ((flags & TCP_SYN) == 0 || (flags & TCP_ACK) == 0 || ack != tcb->snd_nxt) {
                    __ctx->diagnostics.tcp_dropped++;
                    return Status::Malformed;
                }
                tcb->rcv_nxt = seq + 1;   // consume peer SYN
                process_ack(tcb, ack, window);
                tcb->state = TcpState::Established;
                __ctx->diagnostics.tcp_connections_opened++;
                return emit_ack(__ctx, tcb);
            }
            case TcpState::SynReceived: {
                // Expect the final ACK of the handshake.
                if ((flags & TCP_ACK) == 0 || ack != tcb->snd_nxt) {
                    __ctx->diagnostics.tcp_dropped++;
                    return Status::Malformed;
                }
                process_ack(tcb, ack, window);
                tcb->state = TcpState::Established;
                __ctx->diagnostics.tcp_connections_opened++;
                return Status::Ok;
            }
            case TcpState::Established: {
                if ((flags & TCP_ACK) != 0)
                    process_ack(tcb, ack, window);
                bool need_ack = accept_data(tcb, seq, data, data_len);
                // A FIN that arrives in order (after any carried data) advances
                // rcv_nxt by one and moves us into passive close.
                if ((flags & TCP_FIN) != 0 && seq_leq(seq + data_len, tcb->rcv_nxt)) {
                    tcb->rcv_nxt++;
                    tcb->fin_received = true;
                    tcb->state = TcpState::CloseWait;
                    need_ack = true;
                }
                if (need_ack)
                    return emit_ack(__ctx, tcb);
                return Status::Ok;
            }
            case TcpState::FinWait1: {
                if ((flags & TCP_ACK) != 0)
                    process_ack(tcb, ack, window);
                (void)accept_data(tcb, seq, data, data_len);
                const bool our_fin_acked = tcb->snd_una == tcb->snd_nxt;
                const bool peer_fin = (flags & TCP_FIN) != 0 && seq_leq(seq + data_len, tcb->rcv_nxt);
                if (peer_fin) {
                    tcb->rcv_nxt++;
                    tcb->fin_received = true;
                    (void)emit_ack(__ctx, tcb);
                    if (our_fin_acked)
                        enter_time_wait(tcb);   // both FINs done
                    else
                        tcb->state = TcpState::Closing;   // simultaneous close
                    return Status::Ok;
                }
                if (our_fin_acked)
                    tcb->state = TcpState::FinWait2;
                return Status::Ok;
            }
            case TcpState::FinWait2: {
                if ((flags & TCP_ACK) != 0)
                    process_ack(tcb, ack, window);
                (void)accept_data(tcb, seq, data, data_len);
                if ((flags & TCP_FIN) != 0 && seq_leq(seq + data_len, tcb->rcv_nxt)) {
                    tcb->rcv_nxt++;
                    tcb->fin_received = true;
                    (void)emit_ack(__ctx, tcb);
                    enter_time_wait(tcb);
                }
                return Status::Ok;
            }
            case TcpState::Closing: {
                if ((flags & TCP_ACK) != 0)
                    process_ack(tcb, ack, window);
                if (tcb->snd_una == tcb->snd_nxt)
                    enter_time_wait(tcb);
                return Status::Ok;
            }
            case TcpState::CloseWait: {
                if ((flags & TCP_ACK) != 0)
                    process_ack(tcb, ack, window);
                return Status::Ok;
            }
            case TcpState::LastAck: {
                if ((flags & TCP_ACK) != 0)
                    process_ack(tcb, ack, window);
                if (tcb->snd_una == tcb->snd_nxt)
                    recycle_tcb(tcb);   // passive close complete
                return Status::Ok;
            }
            case TcpState::TimeWait: {
                // Acknowledge retransmitted peer FINs; recycled by tcp_pump at 2*MSL.
                if ((flags & TCP_FIN) != 0)
                    (void)emit_ack(__ctx, tcb);
                return Status::Ok;
            }
            default:
                __ctx->diagnostics.tcp_dropped++;
                return Status::Malformed;
        }
    }

    Status tcp_pump(Context *__ctx) noexcept {
        if (__ctx == nullptr)
            return Status::InvalidArgument;
        // Retransmit, RTT sampling, and TIME_WAIT recycling run only in ordinary
        // (non-IRQ) context, reusing the same boundary as the rest of the protocol
        // path. It MUST NOT be driven from an IRQ handler.
        if (!ordinary_context()) {
            __ctx->diagnostics.irq_context_rejected++;
            return Status::UnsupportedContext;
        }
        const timer::tick_t now = timer::ticks();
        for (uint32_t i = 0; i < TCP_CONNECTION_CAPACITY; i++) {
            TcpControlBlock &tcb = g_tcbs[i];
            if (!tcb.active || tcb.owner != __ctx)
                continue;
            if (tcb.state == TcpState::TimeWait) {
                if ((int64_t)(now - tcb.time_wait_deadline_tick) >= 0)
                    recycle_tcb(&tcb);
                continue;
            }
            for (uint32_t j = 0; j < TCP_RETX_QUEUE_CAPACITY; j++) {
                TcpRetxSegment &e = tcb.retx[j];
                if (!e.in_use)
                    continue;
                if ((int64_t)(now - e.rto_deadline_tick) < 0)
                    continue;
                if (e.retransmit_count >= TCP_MAX_RETRANSMIT) {
                    // Exceeded the bounded retransmit limit: reset/abandon the
                    // connection and recycle its TCB.
                    reset_connection(__ctx, &tcb);
                    break;
                }
                // Retransmit with the original seq; mark retransmitted so Karn's
                // algorithm excludes it from RTT sampling. Exponential backoff
                // doubles the connection RTO (bounded by TCP_RTO_MAX) without
                // touching srtt/rttvar.
                const uint32_t acknum = (e.flags & TCP_ACK) != 0 ? tcb.rcv_nxt : 0;
                (void)transmit_segment(__ctx, &tcb, e.seq, acknum, e.flags, e.data, e.data_len);
                e.retransmitted = true;
                e.retransmit_count++;
                tcb.rto = clamp_rto(tcb.rto << 1);
                e.rto_deadline_tick = now + tcb.rto;
                __ctx->diagnostics.tcp_retransmits++;
            }
        }
        return Status::Ok;
    }
}   // namespace net
NAMESPACE_BIGOS_END

#ifdef BIGOS_TCP_PATH_SMOKE
namespace {
    // Kernel-internal bounded TCP local-address closed-loop smoke. It lives in the
    // anonymous namespace so it can reach the TCB pool and helpers for deterministic
    // edge-path coverage (retransmit deadline poke, raw out-of-order/duplicate
    // injection). No user-visible ABI is exposed.
    bigos::net::Context g_smoke_ctx = {};
    uint8_t g_smoke_seg[TCP_HEADER_LEN + bigos::net::TCP_MSS] = {};

    void smoke_fail(const char *__reason) noexcept {
        bigos::serial_puts("BIGOS_TCP_PATH_FAILED ");
        bigos::serial_puts(__reason);
        bigos::serial_puts("\n");
    }

    // Freestanding byte comparison; memcmp is not provided by the kernel runtime.
    bool bytes_equal(const uint8_t *__a, const uint8_t *__b, uint32_t __n) noexcept {
        for (uint32_t i = 0; i < __n; i++) {
            if (__a[i] != __b[i])
                return false;
        }
        return true;
    }

    // Build a raw TCP segment (checksum over the normalized local_ipv4 pseudo
    // header) and deliver it straight to handle_tcp, simulating a peer segment.
    bigos::net::Status inject_seg(uint16_t __sport, uint16_t __dport, uint32_t __seq, uint32_t __ack, uint8_t __flags,
        const uint8_t *__data, uint16_t __dlen) noexcept {
        const bigos::net::Ipv4Address lip = g_smoke_ctx.config.local_ipv4;
        uint8_t *seg = g_smoke_seg;
        write_be16(seg + 0, __sport);
        write_be16(seg + 2, __dport);
        write_be32(seg + 4, __seq);
        write_be32(seg + 8, __ack);
        seg[12] = (uint8_t)((TCP_HEADER_LEN / 4) << 4);
        seg[13] = __flags;
        write_be16(seg + 14, (uint16_t)bigos::net::TCP_RECV_BUFFER);
        write_be16(seg + 16, 0);
        write_be16(seg + 18, 0);
        if (__dlen != 0 && __data != nullptr)
            memcpy(seg + TCP_HEADER_LEN, __data, __dlen);
        const uint16_t len = (uint16_t)(TCP_HEADER_LEN + __dlen);
        write_be16(seg + 16, tcp_checksum(lip, lip, seg, len));
        return bigos::net::handle_tcp(&g_smoke_ctx, lip, lip, seg, len);
    }

    uint32_t active_slots() noexcept {
        uint32_t n = 0;
        for (uint32_t i = 0; i < bigos::net::TCP_CONNECTION_CAPACITY; i++) {
            if (g_tcbs[i].active)
                n++;
        }
        return n;
    }

    bool run_tcp_smoke() noexcept {
        using namespace bigos::net;
        tcp_reset_state();
        g_smoke_ctx = {};

        Config config = {};
        config.local_ipv4 = make_ipv4(10, 0, 2, 15);
        config.netmask = make_ipv4(255, 255, 255, 0);
        config.mtu = DEFAULT_MTU;
        if (init(&g_smoke_ctx, nullptr, &config) != Status::Ok || state(&g_smoke_ctx) != State::LoopbackReady) {
            smoke_fail("init");
            return false;
        }
        const Ipv4Address lip = config.local_ipv4;
        const uint16_t listen_port = 7000;
        const uint16_t client_port = 40000;

        // Phase A: three-way handshake (passive + active). The synchronous loopback
        // completes both sides to Established within tcp_open.
        TcpControlBlock *server = nullptr;
        if (tcp_listen(&g_smoke_ctx, lip, listen_port, &server) != Status::Ok || server == nullptr) {
            smoke_fail("listen");
            return false;
        }
        TcpControlBlock *client = nullptr;
        if (tcp_open(&g_smoke_ctx, lip, client_port, lip, listen_port, &client) != Status::Ok || client == nullptr) {
            smoke_fail("open");
            return false;
        }
        if (client->state != TcpState::Established || server->state != TcpState::Established) {
            smoke_fail("handshake-established");
            return false;
        }
        if (g_smoke_ctx.diagnostics.tcp_connections_opened < 2 || g_smoke_ctx.diagnostics.tcp_segments_tx == 0 ||
            g_smoke_ctx.diagnostics.tcp_segments_rx == 0) {
            smoke_fail("handshake-counters");
            return false;
        }

        // Phase A: in-order bidirectional data delivery.
        const uint8_t c2s[] = {'h', 'i', '-', 's'};
        uint32_t sent = 0;
        if (tcp_send(&g_smoke_ctx, client, c2s, sizeof(c2s), &sent) != Status::Ok || sent != sizeof(c2s)) {
            smoke_fail("send-c2s");
            return false;
        }
        uint8_t rx[16] = {};
        uint32_t got = 0;
        if (tcp_receive(server, rx, sizeof(rx), &got) != Status::Ok || got != sizeof(c2s) ||
            !bytes_equal(rx, c2s, sizeof(c2s))) {
            smoke_fail("recv-c2s");
            return false;
        }
        const uint8_t s2c[] = {'y', 'o'};
        sent = 0;
        if (tcp_send(&g_smoke_ctx, server, s2c, sizeof(s2c), &sent) != Status::Ok || sent != sizeof(s2c)) {
            smoke_fail("send-s2c");
            return false;
        }
        got = 0;
        if (tcp_receive(client, rx, sizeof(rx), &got) != Status::Ok || got != sizeof(s2c) ||
            !bytes_equal(rx, s2c, sizeof(s2c))) {
            smoke_fail("recv-s2c");
            return false;
        }

        // Phase A (cont.): clean teardown on the pristine connection. Active close
        // then passive close; then advance past the standard 2*MSL TIME_WAIT and
        // confirm every slot recycles. This runs before any raw injection so the
        // two real TCBs keep consistent sequence spaces.
        const uint32_t closed_before = g_smoke_ctx.diagnostics.tcp_connections_closed;
        if (tcp_close(&g_smoke_ctx, client) != Status::Ok) {
            smoke_fail("close-active");
            return false;
        }
        if (tcp_close(&g_smoke_ctx, server) != Status::Ok) {
            smoke_fail("close-passive");
            return false;
        }
        if (g_smoke_ctx.diagnostics.tcp_connections_closed < closed_before + 2) {
            smoke_fail("close-counter");
            return false;
        }
        // Drive tcp_pump past 2*MSL so any TIME_WAIT slot recycles deterministically.
        for (uint32_t i = 0; i < TCP_CONNECTION_CAPACITY; i++) {
            if (g_tcbs[i].active && g_tcbs[i].state == TcpState::TimeWait)
                g_tcbs[i].time_wait_deadline_tick = bigos::timer::ticks();
        }
        (void)tcp_pump(&g_smoke_ctx);
        if (active_slots() != 0) {
            smoke_fail("timewait-recycle");
            return false;
        }

        // Phase B: bounded out-of-order reassembly and duplicate/out-of-window drop
        // on a throwaway server connection driven by raw injected peer segments. The
        // fake segments deliberately desync the sequence space, so this connection
        // is discarded (tcp_reset_state) instead of cleanly closed.
        tcp_reset_state();
        server = nullptr;
        client = nullptr;
        if (tcp_listen(&g_smoke_ctx, lip, listen_port, &server) != Status::Ok ||
            tcp_open(&g_smoke_ctx, lip, client_port, lip, listen_port, &client) != Status::Ok ||
            server->state != TcpState::Established) {
            smoke_fail("reorder-setup");
            return false;
        }
        const uint32_t base = server->rcv_nxt;
        const uint8_t seg_a[] = {'A', 'B'};   // in-order at base
        const uint8_t seg_b[] = {'C', 'D'};   // ahead of base (gap)
        // Deliver the gap segment first: it must be buffered, not delivered.
        (void)inject_seg(
            client_port, listen_port, base + sizeof(seg_a), server->snd_nxt, TCP_ACK, seg_b, sizeof(seg_b));
        bool reorder_buffered = false;
        for (uint32_t i = 0; i < TCP_REORDER_SLOTS; i++) {
            if (server->reorder[i].in_use && server->reorder[i].seq == base + sizeof(seg_a))
                reorder_buffered = true;
        }
        if (!reorder_buffered) {
            smoke_fail("reorder-buffer");
            return false;
        }
        // Now the filling segment: both must merge and deliver in order.
        (void)inject_seg(client_port, listen_port, base, server->snd_nxt, TCP_ACK, seg_a, sizeof(seg_a));
        got = 0;
        if (tcp_receive(server, rx, sizeof(rx), &got) != Status::Ok || got != 4 || rx[0] != 'A' || rx[1] != 'B' ||
            rx[2] != 'C' || rx[3] != 'D') {
            smoke_fail("reorder-merge");
            return false;
        }
        // Duplicate (below rcv_nxt): dropped deterministically, no corruption.
        const uint32_t dropped_before = g_smoke_ctx.diagnostics.tcp_dropped;
        (void)inject_seg(client_port, listen_port, base, server->snd_nxt, TCP_ACK, seg_a, sizeof(seg_a));
        if (g_smoke_ctx.diagnostics.tcp_dropped != dropped_before + 1) {
            smoke_fail("duplicate-drop");
            return false;
        }

        // Phase C: RFC 6298 retransmit, exponential backoff, Karn's algorithm, and
        // reset on exceeding the bounded retransmit limit. Recycle the server so the
        // client's data segment stays unacked, then drive tcp_pump to expiry.
        tcp_reset_state();
        server = nullptr;
        client = nullptr;
        if (tcp_listen(&g_smoke_ctx, lip, listen_port, &server) != Status::Ok ||
            tcp_open(&g_smoke_ctx, lip, client_port, lip, listen_port, &client) != Status::Ok ||
            client->state != TcpState::Established) {
            smoke_fail("retx-setup");
            return false;
        }
        recycle_tcb(server);   // drop the peer so nothing ACKs the client's data
        const uint8_t payload[] = {'r', 't', 'x'};
        sent = 0;
        (void)tcp_send(&g_smoke_ctx, client, payload, sizeof(payload), &sent);
        TcpRetxSegment *entry = nullptr;
        for (uint32_t i = 0; i < TCP_RETX_QUEUE_CAPACITY; i++) {
            if (client->retx[i].in_use) {
                entry = &client->retx[i];
                break;
            }
        }
        if (entry == nullptr) {
            smoke_fail("retx-register");
            return false;
        }
        const uint32_t retx_before = g_smoke_ctx.diagnostics.tcp_retransmits;
        // One expiry: assert a retransmit, Karn exclusion flag, RTO doubled and in
        // range. rto was TCP_RTO_MIN before the first backoff.
        entry->rto_deadline_tick = bigos::timer::ticks();
        const uint32_t rto_before = client->rto;
        (void)tcp_pump(&g_smoke_ctx);
        if (g_smoke_ctx.diagnostics.tcp_retransmits != retx_before + 1 || !entry->retransmitted ||
            client->rto != rto_before * 2 || client->rto < TCP_RTO_MIN || client->rto > TCP_RTO_MAX) {
            smoke_fail("retx-backoff");
            return false;
        }
        // Continue expiring until the bounded retransmit limit resets the connection.
        const uint32_t resets_before = g_smoke_ctx.diagnostics.tcp_resets;
        for (uint32_t i = 0; i < TCP_MAX_RETRANSMIT + 2; i++) {
            for (uint32_t j = 0; j < TCP_CONNECTION_CAPACITY; j++) {
                for (uint32_t k = 0; k < TCP_RETX_QUEUE_CAPACITY; k++) {
                    if (g_tcbs[j].active && g_tcbs[j].retx[k].in_use)
                        g_tcbs[j].retx[k].rto_deadline_tick = bigos::timer::ticks();
                }
            }
            (void)tcp_pump(&g_smoke_ctx);
        }
        if (g_smoke_ctx.diagnostics.tcp_resets < resets_before + 1 || active_slots() != 0) {
            smoke_fail("retx-reset");
            return false;
        }

        // Phase D: connection table full is deterministic TableFull + tcp_dropped.
        tcp_reset_state();
        TcpControlBlock *fill = nullptr;
        for (uint32_t i = 0; i < TCP_CONNECTION_CAPACITY; i++) {
            if (tcp_listen(&g_smoke_ctx, lip, (uint16_t)(9000 + i), &fill) != Status::Ok) {
                smoke_fail("table-fill");
                return false;
            }
        }
        const uint32_t dropped_pre_full = g_smoke_ctx.diagnostics.tcp_dropped;
        if (tcp_listen(&g_smoke_ctx, lip, 9500, &fill) != Status::TableFull ||
            g_smoke_ctx.diagnostics.tcp_dropped != dropped_pre_full + 1) {
            smoke_fail("table-full");
            return false;
        }

        // Phase E: an illegal/unmatched segment to a non-listening port drops
        // deterministically without establishing anything.
        tcp_reset_state();
        const uint32_t dropped_pre_illegal = g_smoke_ctx.diagnostics.tcp_dropped;
        if (inject_seg(1234, 5678, 100, 0, TCP_ACK, nullptr, 0) != Status::NotBound ||
            g_smoke_ctx.diagnostics.tcp_dropped != dropped_pre_illegal + 1) {
            smoke_fail("illegal-segment");
            return false;
        }

        tcp_reset_state();
        return true;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace net {
    void tcp_path_smoke_entry(void *) noexcept {
        if (run_tcp_smoke())
            bigos::serial_puts("BIGOS_TCP_PATH_PASSED\n");
    }
}   // namespace net
NAMESPACE_BIGOS_END
#endif   // BIGOS_TCP_PATH_SMOKE
