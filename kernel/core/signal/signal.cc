#include <bigos/signal.h>

#include <string.h>
#include <bigos/errno.h>
#include <bigos/proc.h>
#include <bigos/user_mode.h>
#include <irq/interrupt.h>

namespace bigos::signal {
    namespace {
        // RFLAGS bits the kernel preserves from a user-controlled sigreturn frame.
        // Only arithmetic/direction flags survive; IF is forced on and the
        // reserved bit 1 is set. IOPL, NT, TF and friends are cleared so a forged
        // frame can never raise privilege through rflags.
        constexpr uint64_t RFLAGS_USER_SAFE = 0xCD5ull;   // CF PF AF ZF SF DF OF
        constexpr uint64_t RFLAGS_IF = 1ull << 9;
        constexpr uint64_t RFLAGS_RESERVED_ONE = 1ull << 1;

        // User-handler signal frame placement: leave the System V red zone below
        // the interrupted rsp untouched, then carve the frame and 16-byte align.
        constexpr uint64_t USER_RED_ZONE = 128;
        constexpr uint64_t STACK_ALIGN = 16;

        int lowest_set_signo(SigSet __set) noexcept {
            if (__set == 0)
                return 0;
            int index = __builtin_ctzll(__set);
            return index + 1;
        }

        // Reads __len bytes from the current user address space in <=128 byte
        // chunks (copy_current_user_buffer caps each call). Returns false on the
        // first failing chunk so the caller can deterministically terminate.
        bool read_user_chunked(uint64_t __addr, void *__dst, uint64_t __len) noexcept {
            auto *out = (uint8_t *)__dst;
            uint64_t offset = 0;
            while (offset < __len) {
                uint64_t chunk = __len - offset;
                if (chunk > 64)
                    chunk = 64;
                if (!bigos::proc::copy_current_user_buffer(__addr + offset, out + offset, chunk))
                    return false;
                offset += chunk;
            }
            return true;
        }
    }   // namespace

    bool valid_signo(int __signo) noexcept {
        return __signo >= SIG_MIN && __signo <= SIG_MAX;
    }

    bool is_uncatchable(int __signo) noexcept {
        return __signo == SIGKILL;
    }

    bool is_unblockable(int __signo) noexcept {
        return __signo == SIGKILL;
    }

    DefaultAction default_action(int __signo) noexcept {
        switch (__signo) {
            case SIGCHLD:
                return DefaultAction::Ignore;
            default:
                return DefaultAction::Terminate;
        }
    }

    SigSet signo_bit(int __signo) noexcept {
        if (!valid_signo(__signo))
            return 0;
        return 1ull << (__signo - 1);
    }

    void init_state(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        __process->sig_pending = 0;
        __process->sig_mask = 0;
        for (int i = 0; i < SIG_COUNT; i++) {
            __process->sig_disp[i].action = SigAction::Default;
            __process->sig_disp[i].handler = 0;
        }
    }

    void inherit_on_fork(bigos::proc::Process *__child, const bigos::proc::Process *__parent) noexcept {
        if (__child == nullptr || __parent == nullptr)
            return;
        // POSIX: the child inherits the disposition table and blocked mask but its
        // pending set is empty.
        for (int i = 0; i < SIG_COUNT; i++)
            __child->sig_disp[i] = __parent->sig_disp[i];
        __child->sig_mask = __parent->sig_mask;
        __child->sig_pending = 0;
    }

    void reset_handlers_on_exec(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        // exec replaces the image, so user handler entry addresses become invalid
        // and are reset to default. Explicit Ignore dispositions and the blocked
        // mask / pending set are preserved (decision 10).
        for (int i = 0; i < SIG_COUNT; i++) {
            if (__process->sig_disp[i].action == SigAction::Handler) {
                __process->sig_disp[i].action = SigAction::Default;
                __process->sig_disp[i].handler = 0;
            }
        }
    }

    int64_t kill(bigos::proc::Process *__target, int __signo) noexcept {
        if (__target == nullptr || !valid_signo(__signo))
            return -bigos::EINVAL;
        // Set the pending bit. The blocked mask is only consulted at delivery
        // time, so unblockable signals need no special handling here.
        __target->sig_pending |= signo_bit(__signo);
        return 0;
    }

    bool has_deliverable_signal(const bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return false;
        const SigSet unblockable = signo_bit(SIGKILL);
        return ((__process->sig_pending & ~__process->sig_mask) | (__process->sig_pending & unblockable)) != 0;
    }

    int64_t set_disposition(
        bigos::proc::Process *__process, int __signo, const SigDisposition *__new, SigDisposition *__old) noexcept {
        if (__process == nullptr || !valid_signo(__signo))
            return -bigos::EINVAL;
        SigDisposition &slot = __process->sig_disp[__signo - 1];
        if (__old != nullptr)
            *__old = slot;
        if (__new == nullptr)
            return 0;
        // SIGKILL (and any uncatchable signal) cannot be caught or ignored.
        if (is_uncatchable(__signo) && __new->action != SigAction::Default)
            return -bigos::EINVAL;
        slot = *__new;
        return 0;
    }

    int64_t set_mask(bigos::proc::Process *__process, uint64_t __how, SigSet __set, SigSet *__old) noexcept {
        if (__process == nullptr)
            return -bigos::EINVAL;
        if (__old != nullptr)
            *__old = __process->sig_mask;
        // Unblockable signals can never be added to the mask.
        const SigSet unblockable = signo_bit(SIGKILL);
        const SigSet requested = __set & ~unblockable;
        switch (__how) {
            case SIG_BLOCK:
                __process->sig_mask |= requested;
                return 0;
            case SIG_UNBLOCK:
                __process->sig_mask &= ~__set;
                return 0;
            case SIG_SETMASK:
                __process->sig_mask = requested;
                return 0;
            default:
                return -bigos::EINVAL;
        }
    }

    void deliver_pending_to_user(bigos::irq::InterruptFrame *__frame, bigos::proc::Process *__process) noexcept {
        if (__frame == nullptr || __process == nullptr)
            return;

        // Unblockable signals ignore the blocked mask; everything else honors it.
        const SigSet unblockable = signo_bit(SIGKILL);
        const SigSet deliverable =
            (__process->sig_pending & ~__process->sig_mask) | (__process->sig_pending & unblockable);
        if (deliverable == 0)
            return;

        const int signo = lowest_set_signo(deliverable);
        const SigSet bit = signo_bit(signo);
        // Consume the pending bit before acting on it.
        __process->sig_pending &= ~bit;

        const SigDisposition disp = __process->sig_disp[signo - 1];

        // SIGKILL is uncatchable/unblockable: it always terminates regardless of
        // the recorded disposition. Otherwise the disposition decides.
        if (is_uncatchable(signo)) {
            bigos::proc::fault_current_and_exit(-(SIGNAL_TERMINATE_STATUS_BASE + signo));
        }

        if (disp.action == SigAction::Ignore) {
            // Explicit Ignore: pending bit already cleared, return unchanged.
            return;
        }

        if (disp.action == SigAction::Default) {
            if (default_action(signo) == DefaultAction::Terminate)
                bigos::proc::fault_current_and_exit(-(SIGNAL_TERMINATE_STATUS_BASE + signo));
            // Default Ignore (e.g. SIGCHLD): drop and continue.
            return;
        }

        // disp.action == SigAction::Handler:
        // Build an on-user-stack signal frame, then redirect the interrupted
        // context into the handler. Any validation failure is a deterministic
        // terminate (equivalent to default SIGSEGV) and never writes an illegal
        // user address.
        uint64_t frame_addr = __frame->rsp - USER_RED_ZONE - sizeof(SignalFrame);
        frame_addr &= ~(STACK_ALIGN - 1);

        if (frame_addr >= bigos::proc::USER_LOW_HALF_LIMIT ||
            !bigos::proc::validate_user_io_buffer(frame_addr, sizeof(SignalFrame)))
            bigos::proc::fault_current_and_exit(-(SIGNAL_TERMINATE_STATUS_BASE + SIGSEGV));

        SignalFrame sf;
        memset(&sf, 0, sizeof(sf));
        sf.magic = SIGFRAME_MAGIC;
        sf.signo = (uint64_t)signo;
        sf.saved_mask = __process->sig_mask;
        sf.r15 = __frame->r15;
        sf.r14 = __frame->r14;
        sf.r13 = __frame->r13;
        sf.r12 = __frame->r12;
        sf.r11 = __frame->r11;
        sf.r10 = __frame->r10;
        sf.r9 = __frame->r9;
        sf.r8 = __frame->r8;
        sf.rdi = __frame->rdi;
        sf.rsi = __frame->rsi;
        sf.rbp = __frame->rbp;
        sf.rdx = __frame->rdx;
        sf.rcx = __frame->rcx;
        sf.rbx = __frame->rbx;
        sf.rax = __frame->rax;
        sf.rip = __frame->rip;
        sf.rsp = __frame->rsp;
        sf.rflags = __frame->rflags;

        if (!bigos::proc::copy_to_current_user_buffer(frame_addr, &sf, sizeof(sf)))
            bigos::proc::fault_current_and_exit(-(SIGNAL_TERMINATE_STATUS_BASE + SIGSEGV));

        // Block the delivered signal for the duration of the handler; restored by
        // sigreturn from sf.saved_mask.
        __process->sig_mask |= bit;

        // Redirect the interrupted user context into the handler with signo as the
        // System V first argument and rsp pointing at the new frame.
        __frame->rip = disp.handler;
        __frame->rdi = (uint64_t)signo;
        __frame->rsp = frame_addr;
    }

    void sigreturn(bigos::irq::InterruptFrame *__frame) noexcept {
        if (__frame == nullptr)
            return;
        bigos::proc::Process *process = bigos::proc::current_process();
        if (process == nullptr)
            return;

        const uint64_t frame_addr = __frame->rsp;

        SignalFrame sf;
        if (!read_user_chunked(frame_addr, &sf, sizeof(sf)) || sf.magic != SIGFRAME_MAGIC)
            bigos::proc::fault_current_and_exit(-(SIGNAL_TERMINATE_STATUS_BASE + SIGSEGV));

        // Constrain the restored rip/rsp to the user low half; never trust a
        // user-supplied kernel address.
        if (sf.rip >= bigos::proc::USER_LOW_HALF_LIMIT || sf.rsp >= bigos::proc::USER_LOW_HALF_LIMIT)
            bigos::proc::fault_current_and_exit(-(SIGNAL_TERMINATE_STATUS_BASE + SIGSEGV));

        // Restore user-visible general registers.
        __frame->r15 = sf.r15;
        __frame->r14 = sf.r14;
        __frame->r13 = sf.r13;
        __frame->r12 = sf.r12;
        __frame->r11 = sf.r11;
        __frame->r10 = sf.r10;
        __frame->r9 = sf.r9;
        __frame->r8 = sf.r8;
        __frame->rdi = sf.rdi;
        __frame->rsi = sf.rsi;
        __frame->rbp = sf.rbp;
        __frame->rdx = sf.rdx;
        __frame->rcx = sf.rcx;
        __frame->rbx = sf.rbx;
        __frame->rax = sf.rax;
        __frame->rip = sf.rip;
        __frame->rsp = sf.rsp;

        // Force user-mode segment selectors and a sanitized rflags (IF on, no
        // privileged bits) so a forged frame cannot return to a kernel context.
        __frame->cs = bigos::arch::x86::USER_CODE_SELECTOR;
        __frame->ss = bigos::arch::x86::USER_DATA_SELECTOR;
        __frame->rflags = (sf.rflags & RFLAGS_USER_SAFE) | RFLAGS_IF | RFLAGS_RESERVED_ONE;

        // Restore the pre-handler blocked mask (unblockable bits never persist).
        process->sig_mask = sf.saved_mask & ~signo_bit(SIGKILL);
    }
}   // namespace bigos::signal
