#include <bigos/sched.h>

#include <bigos/io.h>
#include <bigos/memory.h>
#ifdef BIGOS_USER_PROGRAM_SMOKE
#include <bigos/proc.h>
#endif
#include <irq/interrupt.h>

// Internal allocator flag: alloc_kernel_pages() only returns mapped, accessible
// backing when pre-paging is requested. A kernel thread stack must be touchable
// immediately, so the scheduler requests pre-paged kernel pages here.
#include "../../mm/memdef.h"

NAMESPACE_BIGOS_BEG
namespace sched {

    // x86_64 cooperative context switch (src/kernel/sched/switch.s). Saves the
    // System V callee-saved set plus rsp at *old_sp and loads new_sp. Must be
    // called with maskable interrupts disabled.
    extern "C" void switch_context(uint64_t *__old_sp, uint64_t __new_sp) noexcept;

    namespace __detail {
        // Stage 4 fixes the default normal kernel thread stack at one page. No
        // smoke/debug build switch changes this page count.
        constexpr uint32_t KERNEL_THREAD_STACK_PAGES = 1;

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
            // Intrusive terminated-list node; deferred reclamation only.
            TCB *term_next;
        };

        // Single-core scheduler state. There is no per-CPU run queue, no SMP
        // balancing, no IPI, and no cross-CPU synchronization.
        TCB *g_current = nullptr;
        TCB *g_idle = nullptr;
        TCB *g_run_head = nullptr;
        TCB *g_run_tail = nullptr;
        TCB *g_terminated_head = nullptr;
        ThreadId g_next_id = INVALID_THREAD_ID + 1;

        // Bounded reschedule intent recorded by the timer IRQ. Stage 4 never acts
        // on it from IRQ return; it is only readable state for cooperative or
        // future scheduling paths.
        volatile uint64_t g_reschedule_intent = 0;

        // The boot/main thread storage. After start() the boot context becomes
        // the scheduler-owned idle thread and reuses its existing kernel stack.
        TCB g_boot_tcb;

        void rq_push(TCB *__t) noexcept {
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
            TCB *t = g_run_head;
            if (t == nullptr)
                return nullptr;
            g_run_head = t->rq_next;
            if (g_run_head == nullptr)
                g_run_tail = nullptr;
            t->rq_next = nullptr;
            return t;
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
        tcb->term_next = nullptr;

        // Build the initial stack frame so the first switch_context resume lands
        // in thread_trampoline with a 16-byte-aligned System V frame. Layout from
        // saved_sp upward: r15, r14, r13, r12, rbx, rbp, return-address, pad.
        const uint64_t stack_top =
            (uint64_t)stack_base + (uint64_t)__detail::KERNEL_THREAD_STACK_PAGES * PAGE_SIZE;
        uint64_t *sp = (uint64_t *)stack_top;
        sp -= 8;
        sp[0] = 0;   // r15
        sp[1] = 0;   // r14
        sp[2] = 0;   // r13
        sp[3] = 0;   // r12
        sp[4] = 0;   // rbx
        sp[5] = 0;   // rbp
        sp[6] = (uint64_t)&__detail::thread_trampoline;   // return address
        sp[7] = 0;   // alignment padding
        tcb->saved_sp = (uint64_t)sp;

        bigos::irq::disableIRQ();
        __detail::rq_push(tcb);
        bigos::irq::enableIRQ();

        return tcb->id;
    }

    void yield() noexcept {
        bigos::irq::disableIRQ();

        __detail::TCB *prev = __detail::g_current;
        __detail::TCB *next = __detail::rq_pop();

        if (next == nullptr) {
            // No peer runnable thread: keep running the current thread (or idle)
            // without corrupting the run queue.
            bigos::irq::enableIRQ();
            return;
        }

        if (prev->state == ThreadState::Running) {
            prev->state = ThreadState::Runnable;
            __detail::rq_push(prev);
        }

        next->state = ThreadState::Running;
        __detail::g_current = next;
        switch_context(&prev->saved_sp, next->saved_sp);

        // Resumed: the thread that switched back to us left interrupts masked.
        bigos::irq::enableIRQ();
    }

    void thread_exit() noexcept {
        bigos::irq::disableIRQ();

        __detail::TCB *prev = __detail::g_current;
        prev->state = ThreadState::Terminated;
        // Remove from runnable scheduling and retain on the terminated list.
        // The current TCB and kernel stack are NOT freed on this exit stack;
        // safe reclamation is deferred to a later lifecycle change.
        prev->term_next = __detail::g_terminated_head;
        __detail::g_terminated_head = prev;

        __detail::TCB *next = __detail::rq_pop();
        if (next == nullptr)
            next = __detail::g_idle;
        else
            next->state = ThreadState::Running;

        __detail::g_current = next;
        switch_context(&prev->saved_sp, next->saved_sp);

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
        __detail::g_boot_tcb.term_next = nullptr;

        __detail::g_idle = &__detail::g_boot_tcb;
        __detail::g_current = &__detail::g_boot_tcb;

        // Idle thread owns halt behavior: run any runnable thread, otherwise
        // hlt until an IRQ wakes the CPU, then re-evaluate.
        for (;;) {
            sched::yield();
#ifdef BIGOS_USER_PROGRAM_SMOKE
            bigos::proc::reap_pending_processes();
#endif
            asm volatile("hlt");
        }
    }

    void on_timer_tick() noexcept {
        // IRQ-context-safe and bounded: record reschedule intent only. No
        // allocation, no IO, no blocking, and no thread switch on IRQ return.
        ++__detail::g_reschedule_intent;
    }
}   // namespace sched
NAMESPACE_BIGOS_END
