#ifndef _BIG_SIGNAL_H
#define _BIG_SIGNAL_H

#include <bigos/types.h>

namespace bigos::irq {
    struct InterruptFrame;
}   // namespace bigos::irq

namespace bigos::proc {
    struct Process;
}   // namespace bigos::proc

namespace bigos::signal {
    // Minimal, fixed (non-realtime) signal numbers. Each maps to a single bit
    // 1ull << (signo - 1) inside a per-process uint64_t pending/mask bitmap, so
    // the highest supported number MUST stay <= 64. Repeated delivery of the
    // same signal before it is taken merges into one pending bit (no realtime
    // queuing). The values reuse the conventional POSIX/Linux numbers.
    constexpr int SIGINT = 2;     // terminal interrupt-like input
    constexpr int SIGKILL = 9;    // uncatchable, unblockable terminate
    constexpr int SIGUSR1 = 10;   // default terminate, user-defined
    constexpr int SIGSEGV = 11;   // default terminate
    constexpr int SIGUSR2 = 12;   // default terminate, user-defined
    constexpr int SIGTERM = 15;   // default terminate
    constexpr int SIGCHLD = 17;   // default ignore (child state change)

    // Valid signal numbers occupy 1..SIG_MAX. SIG_COUNT sizes the per-process
    // disposition table (indexed by signo - 1). SIG_MAX MUST stay <= 64 so the
    // bitmap type below can represent every signal.
    constexpr int SIG_MIN = 1;
    constexpr int SIG_MAX = 31;
    constexpr int SIG_COUNT = SIG_MAX;

    using SigSet = uint64_t;

    // Deterministic default action for a signal with no user handler.
    enum class DefaultAction : uint8_t {
        Terminate = 0,
        Ignore,
    };

    // Per-signal disposition. Handler is only meaningful when action == Handler
    // and holds the user-mode handler entry address.
    enum class SigAction : uint8_t {
        Default = 0,
        Ignore,
        Handler,
    };

    struct SigDisposition {
        SigAction action;
        uint64_t handler;
    };

    // sigprocmask "how" operations (subset).
    enum SigProcMaskHow : uint64_t {
        SIG_BLOCK = 0,
        SIG_UNBLOCK = 1,
        SIG_SETMASK = 2,
    };

    // Terminate-by-signal encodes 128 + signo as a negative process exit/fault
    // status (negative matches the existing fault_reason convention) so the
    // parent's wait / diagnostics can observe which signal killed the process.
    constexpr int64_t SIGNAL_TERMINATE_STATUS_BASE = 128;

    // Magic word stamped into the on-user-stack signal frame so sigreturn can
    // reject an obviously corrupt / forged frame before trusting any field.
    constexpr uint64_t SIGFRAME_MAGIC = 0x42'49'47'53'49'47'46'52ull;   // "BIGSIGFR"

    // On-user-stack signal frame. deliver_pending_to_user writes it below the
    // interrupted user rsp; the handler keeps rsp pointing at it and ends with
    // int 0x80 SYS_SIGRETURN, which restores the saved user-visible context.
    // Only user-visible GP registers plus a constrained rip/rsp/rflags are saved
    // and restored; privileged fields (cs/ss/vector) are never taken from user
    // memory.
    struct SignalFrame {
        uint64_t magic;
        uint64_t signo;
        uint64_t saved_mask;
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rbp;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;
        uint64_t rip;
        uint64_t rsp;
        uint64_t rflags;
    };

    // Fixed-signal queries (all O(1), no allocation, no blocking).
    bool valid_signo(int __signo) noexcept;
    bool is_uncatchable(int __signo) noexcept;   // SIGKILL this stage
    bool is_unblockable(int __signo) noexcept;   // SIGKILL this stage
    DefaultAction default_action(int __signo) noexcept;
    SigSet signo_bit(int __signo) noexcept;

    // Per-process signal-state lifecycle helpers. All operate on inline
    // fixed-size Process fields and never allocate or block.
    void init_state(bigos::proc::Process *__process) noexcept;
    void inherit_on_fork(bigos::proc::Process *__child, const bigos::proc::Process *__parent) noexcept;
    void reset_handlers_on_exec(bigos::proc::Process *__process) noexcept;

    // Deliver signal __signo to __target by setting its pending bit. SIGKILL and
    // other unblockable signals ignore the target mask at selection time (the
    // mask is only consulted during delivery). No permission decision is made
    // here; the SYS_KILL syscall layer enforces cred::may_signal. Returns 0 on
    // success or -EINVAL for an out-of-range signal number. No allocation, no
    // blocking.
    int64_t kill(bigos::proc::Process *__target, int __signo) noexcept;

    // True when __process has at least one pending signal that is currently
    // deliverable (unblocked, or unblockable like SIGKILL). Cheap O(1) gate used
    // by the IRQ-return delivery point before calling deliver_pending_to_user.
    bool has_deliverable_signal(const bigos::proc::Process *__process) noexcept;

    // SYS_SIGACTION backing: update __process disposition for __signo, writing
    // the previous disposition into *__old when non-null. Rejects uncatchable
    // signals and out-of-range numbers with -EINVAL.
    int64_t set_disposition(bigos::proc::Process *__process, int __signo, const SigDisposition *__new,
        SigDisposition *__old) noexcept;

    // SYS_SIGPROCMASK backing: apply __how with __set to the process mask,
    // returning the old mask through *__old when non-null. Unblockable-signal
    // bits are silently dropped from the requested set (documented semantics).
    int64_t set_mask(bigos::proc::Process *__process, uint64_t __how, SigSet __set, SigSet *__old) noexcept;

    // Selects the lowest-numbered unblocked pending signal of the current process
    // (the one whose user context is in __frame) and acts on it: default
    // Terminate / SIGKILL routes through the existing exit-to-reaper lifecycle
    // (encoding the signal number into the exit/fault status); Ignore clears the
    // pending bit and continues; a user handler builds an on-user-stack signal
    // frame and rewrites __frame to jump to the handler. MUST only be called on a
    // user-mode interrupted frame. No allocation, no blocking.
    void deliver_pending_to_user(bigos::irq::InterruptFrame *__frame, bigos::proc::Process *__process) noexcept;

    // SYS_SIGRETURN backing: restore the user-visible registers and constrained
    // rip/rsp/rflags plus the old mask from the on-user-stack signal frame at
    // __frame->rsp. Forces user-mode constraints and never returns to a kernel
    // privileged context from user-controlled data. A corrupt/forged frame
    // deterministically terminates the process.
    void sigreturn(bigos::irq::InterruptFrame *__frame) noexcept;
}   // namespace bigos::signal

#endif   // _BIG_SIGNAL_H
