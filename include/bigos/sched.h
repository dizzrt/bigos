#ifndef _BIG_SCHED_H
#define _BIG_SCHED_H

#include <bigos/types.h>
#include <bigos/thread.h>
#include <bigos/timer.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    struct InterruptFrame;
}
namespace sched {
    // Invalid/unassigned thread id sentinel.
    constexpr ThreadId INVALID_THREAD_ID = 0;

    constexpr int WAIT_OK = 0;
    constexpr int WAIT_TIMEOUT = -110;
    constexpr int WAIT_INVALID = -22;
    constexpr int WAIT_BLOCK_FORBIDDEN = -1;

    using WaitPredicate = bool (*)(void *__arg) noexcept;

    // Intrusive single-core wait queue. The opaque links point at scheduler-owned
    // TCBs; callers own only the queue head/tail storage and never allocate nodes
    // on the sleep/wakeup fast path.
    struct WaitQueue {
        void *head;
        void *tail;
    };

    // Create a kernel thread.
    //
    // Non-interrupt-context only. This path allocates a thread control block and
    // a kernel stack through the ordinary phase 3 allocator contract, so it MUST
    // NOT be called from any IRQ handler, the timer on_tick() path, irq_dispatch,
    // or any CPU exception handler. Single-core only: there is no SMP placement,
    // affinity, per-CPU run queue, or cross-CPU migration.
    //
    // Returns the new thread id on success, or INVALID_THREAD_ID on failure.
    // The created thread starts in the Runnable state.
    ThreadId create_kernel_thread(ThreadEntry __entry, void *__arg) noexcept;

    // Cooperative yield. Non-interrupt-context only.
    //
    // If at least one other non-idle runnable thread exists, the current thread
    // is placed back on the run queue and the scheduler switches to the next
    // runnable thread in round-robin order. Otherwise the current thread keeps
    // running (or the idle thread runs) without corrupting the run queue.
    void yield() noexcept;

    // Terminate the current thread. Non-interrupt-context only.
    //
    // Marks the current thread Terminated, removes it from runnable scheduling,
    // and switches away. The current thread's TCB and kernel stack are NOT freed
    // on this exit stack; reclamation is deferred to a later lifecycle change.
    // This function does not return.
    [[noreturn]] void thread_exit() noexcept;

    // Enter the scheduler. Non-interrupt-context only.
    //
    // Adopts the current boot/main execution context as a scheduler thread,
    // registers the scheduler-owned idle thread, and begins running threads. Must
    // be called with maskable interrupts enabled so timer IRQ0 can wake the idle
    // thread's hlt. This is single-core only and does not return to its caller in
    // the normal sense; the boot thread becomes a scheduled thread.
    void start() noexcept;

    // Context guard used by blocking APIs. Blocking is allowed only from ordinary
    // running kernel-thread context after sched::start(), with maskable IRQs
    // enabled and outside IRQ/exception/syscall/fatal/scheduler critical paths.
    bool can_block() noexcept;
    void enter_nonblocking_context() noexcept;
    void leave_nonblocking_context() noexcept;
    void disable_preemption() noexcept;
    void enable_preemption() noexcept;
    bool preemption_enabled() noexcept;
    bool reschedule_pending() noexcept;

    void init_wait_queue(WaitQueue *__queue) noexcept;
    bool wait_queue_empty(const WaitQueue *__queue) noexcept;

    // Wait until predicate is true or the queue is woken. timeout_ticks == 0
    // means no timeout. A positive timeout returns WAIT_TIMEOUT when the deadline
    // expires. The predicate is checked with maskable IRQs disabled to avoid a
    // missed producer wakeup between the caller's empty check and enqueue.
    int wait_queue_wait_until(WaitQueue *__queue, WaitPredicate __predicate, void *__arg,
                              timer::tick_t __timeout_ticks = 0) noexcept;

    // Allocation-free wakeups. IRQ handlers may call these bounded helpers, but
    // they only make waiters runnable for a later cooperative scheduling point.
    uint32_t wake_one(WaitQueue *__queue) noexcept;
    uint32_t wake_all(WaitQueue *__queue) noexcept;

    // Cooperative scheduler sleep. The current thread becomes non-runnable until
    // the monotonic tick deadline expires. It returns WAIT_TIMEOUT on expiry.
    int sleep_for(timer::tick_t __ticks) noexcept;

    // IRQ-context-safe bounded scheduler tick hook.
    //
    // Called from the timer IRQ0 path after on_tick(). It performs bounded
    // time-slice accounting, records reschedule intent, and wakes sleepers. It
    // MUST NOT allocate, free, block, do bulk IO, or switch threads directly.
    void on_timer_tick() noexcept;

    // External IRQ-return bridge. irq_dispatch calls this only after the
    // registered handler completes and the single i8259 EOI has been sent.
    // Exceptions and int 0x80 syscalls do not enter this path.
    void maybe_preempt_on_irq_return(irq::InterruptFrame *__frame) noexcept;
}   // namespace sched
NAMESPACE_BIGOS_END

#endif   // _BIG_SCHED_H
