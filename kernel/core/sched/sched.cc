#include <bigos/sched.h>

#include <bigos/arch_context.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#ifdef BIGOS_USER_PROCESS
#include <bigos/proc.h>
#endif
#include <irq/interrupt.h>

// Internal allocator flag: alloc_kernel_pages() only returns mapped, accessible
// backing when pre-paging is requested. A kernel thread stack must be touchable
// immediately, so the scheduler requests pre-paged kernel pages here.
#include "../../mm/memdef.h"

NAMESPACE_BIGOS_BEG
namespace sched {

    namespace __detail {
        // Stage 4 fixes the default normal kernel thread stack at one page. No
        // smoke/debug build switch changes this page count.
        constexpr uint32_t KERNEL_THREAD_STACK_PAGES = 1;
        constexpr uint32_t DEFAULT_TIME_SLICE_TICKS = 2;
        constexpr int32_t DEFAULT_STATIC_PRIORITY = 0;
        constexpr uint32_t DEFAULT_POLICY_SLOT = 0;

        static_assert(KERNEL_THREAD_STACK_PAGES > 0);
        static_assert(DEFAULT_TIME_SLICE_TICKS > 0);

        struct TCB {
            ThreadId id;
            ThreadState state;
            // Saved stack pointer (the cooperative context pointer). Ownership of
            // this value belongs to the scheduler while the thread is not running.
            uint64_t saved_sp;
            // Kernel stack allocation range recorded for this thread.
            void *stack_base;
            uint32_t stack_pages;
            ThreadEntry entry;
            void *arg;
            // Intrusive run-queue node; lifetime owned by this TCB so the run
            // queue never allocates in IRQ context.
            TCB *rq_next;
            // Intrusive wait/sleep nodes. A thread may belong to at most one
            // explicit wait queue and one timeout tracking list at a time.
            TCB *wait_next;
            WaitQueue *wait_queue;
            TCB *sleep_next;
            timer::tick_t deadline_tick;
            int wait_result;
            // Stage 11 bounded scheduling metadata. Priority/policy are reserved
            // hooks; default selection remains single-core round-robin.
            uint32_t time_slice_remaining;
            int32_t static_priority;
            uint32_t policy_slot;
            // Intrusive terminated-list node; deferred reclamation only.
            TCB *term_next;
            // The user process (bigos::proc::Process*) this kernel thread runs in
            // ring3, or nullptr for pure kernel threads (idle, smoke helpers, the
            // boot thread). The proc layer's ring3 context (current process, user
            // CR3, TSS rsp0) is global, so when a thread is switched back in the
            // scheduler must re-establish it from this field; otherwise a thread
            // that ran in between (e.g. a forked child that exited) leaves the
            // wrong process / address space active. Untyped here to avoid a
            // scheduler dependency on the proc layout.
            void *user_process;
        };

        // Single-core scheduler state. There is no per-CPU run queue, no SMP
        // balancing, no IPI, and no cross-CPU synchronization.
        TCB *g_current = nullptr;
        TCB *g_idle = nullptr;
        TCB *g_run_head = nullptr;
        TCB *g_run_tail = nullptr;
        TCB *g_sleep_head = nullptr;
        TCB *g_terminated_head = nullptr;
        ThreadId g_next_id = INVALID_THREAD_ID + 1;
        bool g_scheduler_started = false;
        volatile uint32_t g_nonblocking_depth = 0;
        volatile uint32_t g_scheduler_critical_depth = 0;
        volatile uint32_t g_preemption_disable_depth = 0;

        // Bounded reschedule intent recorded by the timer IRQ. The intent is
        // consumed only at explicit scheduler-owned safe boundaries.
        volatile uint64_t g_reschedule_intent = 0;
        volatile bool g_reschedule_pending = false;
        volatile uint64_t g_slice_expired_events = 0;
        volatile uint64_t g_deferred_preemption_events = 0;
        volatile uint64_t g_irq_return_preemptions = 0;

        // The boot/main thread storage. After start() the boot context becomes
        // the scheduler-owned idle thread and reuses its existing kernel stack.
        TCB g_boot_tcb;

        uint64_t read_rflags() noexcept {
            uint64_t flags;
            asm volatile("pushfq; popq %0" : "=r"(flags)::"memory");
            return flags;
        }

        bool interrupts_enabled() noexcept {
            constexpr uint64_t RFLAGS_IF = 1ull << 9;
            return (read_rflags() & RFLAGS_IF) != 0;
        }

        void enter_scheduler_critical() noexcept {
            ++g_preemption_disable_depth;
            ++g_scheduler_critical_depth;
        }

        void leave_scheduler_critical() noexcept {
            if (g_scheduler_critical_depth > 0)
                --g_scheduler_critical_depth;
            if (g_preemption_disable_depth > 0)
                --g_preemption_disable_depth;
        }

        void request_reschedule() noexcept {
            ++g_reschedule_intent;
            g_reschedule_pending = true;
            if (g_preemption_disable_depth > 0)
                ++g_deferred_preemption_events;
        }

        bool has_runnable_peer() noexcept {
            TCB *cur = g_run_head;
            while (cur != nullptr) {
                if (cur->state == ThreadState::Runnable)
                    return true;
                cur = cur->rq_next;
            }
            return false;
        }

        bool is_ordinary_running_thread(TCB *__t) noexcept {
            return __t != nullptr && __t != g_idle && __t->state == ThreadState::Running;
        }

        void refresh_time_slice(TCB *__t) noexcept {
            if (__t != nullptr && __t != g_idle)
                __t->time_slice_remaining = DEFAULT_TIME_SLICE_TICKS;
        }

        // After a switch_context returns into a thread, re-establish the proc
        // layer's global ring3 context (current process, user CR3, TSS rsp0) for
        // the now-current thread. This must run on every resume path because a
        // thread that ran in between may have left a different (or no) user
        // process active. A no-op for kernel threads (user_process == nullptr).
        void restore_user_context_on_resume() noexcept {
#ifdef BIGOS_USER_PROCESS
            if (g_current != nullptr && g_current->user_process != nullptr)
                bigos::proc::restore_current_user_context((bigos::proc::Process *)g_current->user_process);
#endif
        }

        void prepare_address_space_for_next(TCB *__next) noexcept {
#ifdef BIGOS_USER_PROCESS
            bigos::proc::prepare_context_switch_to(
                __next != nullptr ? (bigos::proc::Process *)__next->user_process : nullptr);
#else
            (void)__next;
#endif
        }

        void prepare_address_space_before_switch(TCB *__next) noexcept {
#ifdef BIGOS_USER_PROCESS
            // Do not activate a target user CR3 while still running on the
            // outgoing thread's stack. Some outgoing stacks (notably idle's boot
            // stack) are not mapped in the target user root. The incoming user
            // thread restores its CR3/TSS after switch_context resumes on its own
            // kernel stack.
            if (__next != nullptr && __next->user_process != nullptr)
                return;
#endif
            prepare_address_space_for_next(__next);
        }

        bool can_preempt_from_irq_return(const irq::InterruptFrame *__frame) noexcept {
            return arch_context::is_kernel_irq_return_context(__frame) && g_scheduler_started &&
                   is_ordinary_running_thread(g_current) && g_reschedule_pending && g_preemption_disable_depth == 0 &&
                   g_scheduler_critical_depth == 0 && g_nonblocking_depth <= 1 && has_runnable_peer();
        }

        void rq_push(TCB *__t) noexcept {
            if (__t == nullptr || __t->state != ThreadState::Runnable)
                return;
            __t->rq_next = nullptr;
            if (g_run_tail == nullptr) {
                g_run_head = __t;
                g_run_tail = __t;
            } else {
                g_run_tail->rq_next = __t;
                g_run_tail = __t;
            }
        }

        TCB *rq_pop() noexcept {
            while (g_run_head != nullptr) {
                TCB *t = g_run_head;
                g_run_head = t->rq_next;
                if (g_run_head == nullptr)
                    g_run_tail = nullptr;
                t->rq_next = nullptr;
                if (t->state == ThreadState::Runnable)
                    return t;
            }
            return nullptr;
        }

        void wait_queue_push_locked(WaitQueue *__queue, TCB *__t) noexcept {
            __t->wait_next = nullptr;
            __t->wait_queue = __queue;
            if (__queue->tail == nullptr) {
                __queue->head = __t;
                __queue->tail = __t;
            } else {
                ((TCB *)__queue->tail)->wait_next = __t;
                __queue->tail = __t;
            }
        }

        void wait_queue_remove_locked(TCB *__t) noexcept {
            WaitQueue *queue = __t->wait_queue;
            if (queue == nullptr)
                return;

            TCB *prev = nullptr;
            TCB *cur = (TCB *)queue->head;
            while (cur != nullptr) {
                if (cur == __t) {
                    if (prev == nullptr)
                        queue->head = cur->wait_next;
                    else
                        prev->wait_next = cur->wait_next;
                    if (queue->tail == cur)
                        queue->tail = prev;
                    cur->wait_next = nullptr;
                    cur->wait_queue = nullptr;
                    return;
                }
                prev = cur;
                cur = cur->wait_next;
            }
            __t->wait_queue = nullptr;
            __t->wait_next = nullptr;
        }

        void sleep_push_locked(TCB *__t) noexcept {
            __t->sleep_next = g_sleep_head;
            g_sleep_head = __t;
        }

        void sleep_remove_locked(TCB *__t) noexcept {
            TCB *prev = nullptr;
            TCB *cur = g_sleep_head;
            while (cur != nullptr) {
                if (cur == __t) {
                    if (prev == nullptr)
                        g_sleep_head = cur->sleep_next;
                    else
                        prev->sleep_next = cur->sleep_next;
                    cur->sleep_next = nullptr;
                    return;
                }
                prev = cur;
                cur = cur->sleep_next;
            }
            __t->sleep_next = nullptr;
        }

        bool wake_thread_locked(TCB *__t, int __result) noexcept {
            if (__t == nullptr || __t->state == ThreadState::Terminated || __t->state == ThreadState::Runnable ||
                __t->state == ThreadState::Running || __t->state == ThreadState::Idle)
                return false;

            wait_queue_remove_locked(__t);
            sleep_remove_locked(__t);
            __t->wait_result = __result;
            __t->state = ThreadState::Runnable;
            refresh_time_slice(__t);
            rq_push(__t);
            return true;
        }

        void schedule_blocked_current_locked(TCB *__prev) noexcept {
            TCB *next = rq_pop();
            if (next == nullptr)
                next = g_idle;
            else {
                next->state = ThreadState::Running;
                refresh_time_slice(next);
            }

            g_current = next;
            prepare_address_space_before_switch(next);
            arch_context::switch_kernel_context(&__prev->saved_sp, next->saved_sp);
            restore_user_context_on_resume();
        }

        // New-thread startup path. Entered for the first time through the prepared
        // stack frame after switch_context returns into it. Interrupts are still
        // masked from the switching thread, so enable them before running entry.
        extern "C" void thread_trampoline() noexcept {
            bigos::irq::enableIRQ();
            TCB *self = g_current;
            self->entry(self->arg);
            sched::thread_exit();
        }
    }   // namespace __detail

    ThreadId create_kernel_thread(ThreadEntry __entry, void *__arg) noexcept {
        if (__entry == nullptr)
            return INVALID_THREAD_ID;

        // Non-interrupt context only: ordinary allocator under the phase 3
        // contract. Never reached from any IRQ handler.
        __detail::TCB *tcb = (__detail::TCB *)kmalloc(sizeof(__detail::TCB));
        if (tcb == nullptr)
            return INVALID_THREAD_ID;

        void *stack_base = alloc_kernel_pages(__detail::KERNEL_THREAD_STACK_PAGES, _GFM_PRE_PAGING);
        if (stack_base == nullptr) {
            // Failure path must not violate the phase 3 allocator contract: free
            // only what this path allocated, in non-interrupt context.
            bigos::free(tcb);
            return INVALID_THREAD_ID;
        }

        tcb->id = __detail::g_next_id++;
        tcb->state = ThreadState::Runnable;
        tcb->stack_base = stack_base;
        tcb->stack_pages = __detail::KERNEL_THREAD_STACK_PAGES;
        tcb->entry = __entry;
        tcb->arg = __arg;
        tcb->rq_next = nullptr;
        tcb->wait_next = nullptr;
        tcb->wait_queue = nullptr;
        tcb->sleep_next = nullptr;
        tcb->deadline_tick = 0;
        tcb->wait_result = WAIT_OK;
        tcb->time_slice_remaining = __detail::DEFAULT_TIME_SLICE_TICKS;
        tcb->static_priority = __detail::DEFAULT_STATIC_PRIORITY;
        tcb->policy_slot = __detail::DEFAULT_POLICY_SLOT;
        tcb->term_next = nullptr;
        tcb->user_process = nullptr;

        // Build the initial stack frame so the first switch_context resume lands
        // in thread_trampoline with a 16-byte-aligned System V frame. Layout from
        // saved_sp upward: r15, r14, r13, r12, rbx, rbp, return-address, pad.
        const uint64_t stack_top = (uint64_t)stack_base + (uint64_t)__detail::KERNEL_THREAD_STACK_PAGES * PAGE_SIZE;
        uint64_t *sp = (uint64_t *)stack_top;
        sp -= 8;
        sp[0] = 0;                                        // r15
        sp[1] = 0;                                        // r14
        sp[2] = 0;                                        // r13
        sp[3] = 0;                                        // r12
        sp[4] = 0;                                        // rbx
        sp[5] = 0;                                        // rbp
        sp[6] = (uint64_t)&__detail::thread_trampoline;   // return address
        sp[7] = 0;                                        // alignment padding
        tcb->saved_sp = (uint64_t)sp;

        bigos::irq::disableIRQ();
        __detail::enter_scheduler_critical();
        __detail::rq_push(tcb);
        __detail::leave_scheduler_critical();
        bigos::irq::enableIRQ();

        return tcb->id;
    }

    void yield() noexcept {
        bigos::irq::disableIRQ();
        __detail::enter_scheduler_critical();

        __detail::TCB *prev = __detail::g_current;
        __detail::TCB *next = __detail::rq_pop();

        if (next == nullptr) {
            // No peer runnable thread: keep running the current thread (or idle)
            // without corrupting the run queue.
            __detail::leave_scheduler_critical();
            bigos::irq::enableIRQ();
            return;
        }

        if (prev->state == ThreadState::Running) {
            prev->state = ThreadState::Runnable;
            __detail::rq_push(prev);
        }

        next->state = ThreadState::Running;
        __detail::refresh_time_slice(next);
        __detail::g_reschedule_pending = false;
        __detail::g_current = next;
        __detail::leave_scheduler_critical();
        __detail::prepare_address_space_before_switch(next);
        arch_context::switch_kernel_context(&prev->saved_sp, next->saved_sp);

        // Resumed: the thread that switched back to us left interrupts masked.
        __detail::restore_user_context_on_resume();
        bigos::irq::enableIRQ();
    }

    void set_current_user_process(void *__process) noexcept {
        bigos::irq::InterruptGuard guard;
        if (__detail::g_current != nullptr)
            __detail::g_current->user_process = __process;
    }

    void thread_exit() noexcept {
        bigos::irq::disableIRQ();
        __detail::enter_scheduler_critical();
        __detail::TCB *prev = __detail::g_current;
        __detail::wait_queue_remove_locked(prev);
        __detail::sleep_remove_locked(prev);
        prev->state = ThreadState::Terminated;
        // Remove from runnable scheduling and retain on the terminated list.
        // The current TCB and kernel stack are NOT freed on this exit stack;
        // safe reclamation is deferred to a later lifecycle change.
        prev->term_next = __detail::g_terminated_head;
        __detail::g_terminated_head = prev;

        __detail::TCB *next = __detail::rq_pop();
        if (next == nullptr)
            next = __detail::g_idle;
        else {
            next->state = ThreadState::Running;
            __detail::refresh_time_slice(next);
        }

        // Terminated threads are never resumed. Do not leave saved_sp pointing
        // into the exit stack, because the process reaper may free that stack
        // after the parent observes the wait status.
        uint64_t discarded_sp = 0;
        prev->saved_sp = 0;

        __detail::g_current = next;
        __detail::leave_scheduler_critical();
        __detail::prepare_address_space_before_switch(next);
        arch_context::switch_kernel_context(&discarded_sp, next->saved_sp);

        // Unreachable: a terminated thread is never scheduled again.
        for (;;)
            asm volatile("hlt");
    }

    void start() noexcept {
        // Adopt the boot/main execution context as the scheduler-owned idle
        // thread, reusing the existing boot kernel stack. Must run with maskable
        // interrupts enabled so timer IRQ0 can wake the idle hlt.
        __detail::g_boot_tcb.id = __detail::g_next_id++;
        __detail::g_boot_tcb.state = ThreadState::Idle;
        __detail::g_boot_tcb.saved_sp = 0;
        __detail::g_boot_tcb.stack_base = nullptr;
        __detail::g_boot_tcb.stack_pages = 0;
        __detail::g_boot_tcb.entry = nullptr;
        __detail::g_boot_tcb.arg = nullptr;
        __detail::g_boot_tcb.rq_next = nullptr;
        __detail::g_boot_tcb.wait_next = nullptr;
        __detail::g_boot_tcb.wait_queue = nullptr;
        __detail::g_boot_tcb.sleep_next = nullptr;
        __detail::g_boot_tcb.deadline_tick = 0;
        __detail::g_boot_tcb.wait_result = WAIT_OK;
        __detail::g_boot_tcb.time_slice_remaining = 0;
        __detail::g_boot_tcb.static_priority = __detail::DEFAULT_STATIC_PRIORITY;
        __detail::g_boot_tcb.policy_slot = __detail::DEFAULT_POLICY_SLOT;
        __detail::g_boot_tcb.term_next = nullptr;
        __detail::g_boot_tcb.user_process = nullptr;

        __detail::g_idle = &__detail::g_boot_tcb;
        __detail::g_current = &__detail::g_boot_tcb;
        __detail::g_scheduler_started = true;

        // Idle thread owns halt behavior: run any runnable thread, otherwise
        // hlt until an IRQ wakes the CPU, then re-evaluate.
        for (;;) {
            sched::yield();
#ifdef BIGOS_USER_PROCESS
            bigos::proc::reap_pending_processes();
#endif
            asm volatile("hlt");
        }
    }

    bool can_block() noexcept {
        return __detail::g_scheduler_started && __detail::g_current != nullptr &&
               __detail::g_current != __detail::g_idle && __detail::g_current->state == ThreadState::Running &&
               __detail::g_nonblocking_depth == 0 && __detail::g_scheduler_critical_depth == 0 &&
               __detail::interrupts_enabled();
    }

    bool can_allocate_in_fault() noexcept {
        // Weaker than can_block(): the CPU page-fault path runs under the
        // nonblocking guard with IF=0 but its frame allocation never blocks.
        // Require only an ordinary running kernel thread (not idle) after start
        // and no scheduler critical section; ignore the nonblocking depth and IF.
        return __detail::g_scheduler_started && __detail::g_current != nullptr &&
               __detail::g_current != __detail::g_idle && __detail::g_current->state == ThreadState::Running &&
               __detail::g_scheduler_critical_depth == 0;
    }

    void enter_nonblocking_context() noexcept {
        ++__detail::g_nonblocking_depth;
    }

    void leave_nonblocking_context() noexcept {
        if (__detail::g_nonblocking_depth > 0)
            --__detail::g_nonblocking_depth;
    }

    void disable_preemption() noexcept {
        ++__detail::g_preemption_disable_depth;
    }

    void enable_preemption() noexcept {
        if (__detail::g_preemption_disable_depth > 0)
            --__detail::g_preemption_disable_depth;
    }

    bool preemption_enabled() noexcept {
        return __detail::g_preemption_disable_depth == 0;
    }

    bool reschedule_pending() noexcept {
        return __detail::g_reschedule_pending;
    }

    void init_wait_queue(WaitQueue *__queue) noexcept {
        if (__queue == nullptr)
            return;
        __queue->head = nullptr;
        __queue->tail = nullptr;
    }

    bool wait_queue_empty(const WaitQueue *__queue) noexcept {
        return __queue == nullptr || __queue->head == nullptr;
    }

    int wait_queue_wait_until(
        WaitQueue *__queue, WaitPredicate __predicate, void *__arg, timer::tick_t __timeout_ticks) noexcept {
        if (__queue == nullptr)
            return WAIT_INVALID;
        if (!can_block())
            return WAIT_BLOCK_FORBIDDEN;

        bigos::irq::disableIRQ();
        __detail::enter_scheduler_critical();

        if (__predicate != nullptr && __predicate(__arg)) {
            __detail::leave_scheduler_critical();
            bigos::irq::enableIRQ();
            return WAIT_OK;
        }

        __detail::TCB *self = __detail::g_current;
        self->wait_result = WAIT_OK;
        __detail::wait_queue_push_locked(__queue, self);
        if (__timeout_ticks > 0) {
            self->deadline_tick = timer::ticks() + __timeout_ticks;
            self->state = ThreadState::Sleeping;
            __detail::sleep_push_locked(self);
        } else {
            self->deadline_tick = 0;
            self->state = ThreadState::Blocked;
        }

        __detail::leave_scheduler_critical();
        __detail::schedule_blocked_current_locked(self);

        // Resumed by wakeup or timeout. The switcher left IRQs masked.
        bigos::irq::enableIRQ();
        return self->wait_result;
    }

    uint32_t wake_one(WaitQueue *__queue) noexcept {
        if (__queue == nullptr)
            return 0;

        bigos::irq::InterruptGuard guard;
        __detail::enter_scheduler_critical();

        __detail::TCB *t = (__detail::TCB *)__queue->head;
        while (t != nullptr && t->state == ThreadState::Terminated) {
            __detail::TCB *next = t->wait_next;
            __detail::wait_queue_remove_locked(t);
            t = next;
        }

        const uint32_t woke = __detail::wake_thread_locked(t, WAIT_OK) ? 1u : 0u;
        __detail::leave_scheduler_critical();
        return woke;
    }

    uint32_t wake_all(WaitQueue *__queue) noexcept {
        if (__queue == nullptr)
            return 0;

        bigos::irq::InterruptGuard guard;
        __detail::enter_scheduler_critical();

        uint32_t count = 0;
        while (__queue->head != nullptr) {
            __detail::TCB *t = (__detail::TCB *)__queue->head;
            if (__detail::wake_thread_locked(t, WAIT_OK))
                ++count;
            else
                __detail::wait_queue_remove_locked(t);
        }

        __detail::leave_scheduler_critical();
        return count;
    }

    int sleep_for(timer::tick_t __ticks) noexcept {
        if (__ticks == 0)
            return WAIT_OK;
        if (!can_block())
            return WAIT_BLOCK_FORBIDDEN;

        bigos::irq::disableIRQ();
        __detail::enter_scheduler_critical();

        __detail::TCB *self = __detail::g_current;
        self->wait_result = WAIT_TIMEOUT;
        self->deadline_tick = timer::ticks() + __ticks;
        self->state = ThreadState::Sleeping;
        __detail::sleep_push_locked(self);

        __detail::leave_scheduler_critical();
        __detail::schedule_blocked_current_locked(self);

        bigos::irq::enableIRQ();
        return self->wait_result;
    }

    void maybe_preempt_on_irq_return(irq::InterruptFrame *__frame) noexcept {
        if (!__detail::can_preempt_from_irq_return(__frame))
            return;

        __detail::enter_scheduler_critical();
        __detail::TCB *prev = __detail::g_current;
        __detail::TCB *next = __detail::rq_pop();
        if (next == nullptr) {
            __detail::leave_scheduler_critical();
            return;
        }

        if (prev->state == ThreadState::Running) {
            prev->state = ThreadState::Runnable;
            __detail::rq_push(prev);
        }

        next->state = ThreadState::Running;
        __detail::refresh_time_slice(next);
        __detail::g_current = next;
        __detail::g_reschedule_pending = false;
        ++__detail::g_irq_return_preemptions;
        __detail::leave_scheduler_critical();

        arch_context::switch_kernel_context(&prev->saved_sp, next->saved_sp);
        __detail::restore_user_context_on_resume();
    }

    void on_timer_tick() noexcept {
        // IRQ-context-safe and bounded: no allocation, no IO, no blocking, and
        // no direct thread switch. Context switching is deferred to explicit
        // scheduler-owned return boundaries after EOI. Slice expiry updates
        // g_reschedule_intent through request_reschedule().
        const timer::tick_t now = timer::ticks();
        __detail::TCB *prev = nullptr;
        __detail::TCB *cur = __detail::g_sleep_head;
        while (cur != nullptr) {
            __detail::TCB *next = cur->sleep_next;
            if (now >= cur->deadline_tick) {
                if (prev == nullptr)
                    __detail::g_sleep_head = next;
                else
                    prev->sleep_next = next;
                cur->sleep_next = nullptr;
                __detail::wait_queue_remove_locked(cur);
                if (cur->state == ThreadState::Sleeping) {
                    cur->wait_result = WAIT_TIMEOUT;
                    cur->state = ThreadState::Runnable;
                    __detail::rq_push(cur);
                }
            } else {
                prev = cur;
            }
            cur = next;
        }

        __detail::TCB *current = __detail::g_current;
        if (!__detail::is_ordinary_running_thread(current))
            return;

        if (current->time_slice_remaining > 0)
            --current->time_slice_remaining;

        if (current->time_slice_remaining == 0) {
            ++__detail::g_slice_expired_events;
            __detail::request_reschedule();
        }
    }
}   // namespace sched
NAMESPACE_BIGOS_END
