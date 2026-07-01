#ifndef _BIG_SCHED_H
#define _BIG_SCHED_H

#include <bigos/types.h>
#include <bigos/percpu.h>
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

    // Upper bound on the number of wait queues a single thread may register on
    // at once through wait_queue_wait_any(). It is sized to cover a bounded
    // multiplexing set where each descriptor may contribute at most a read and a
    // write queue after de-duplication. Each waiting thread owns this many poll
    // nodes in stable per-thread storage so multi-queue registration/wakeup is
    // allocation-free.
    constexpr uint32_t POLL_MAX_WAIT_QUEUES = 32;

    // Intrusive scheduler wait queue. The opaque links point at scheduler-owned
    // TCBs; callers own only the queue head/tail storage and never allocate nodes
    // on the sleep/wakeup fast path. The scheduler serializes queue membership
    // with an IRQ-safe lock and re-enqueues woken threads through their CPU-owned
    // scheduler domain. This is a scheduler wait/wakeup boundary only; it is not
    // a generic SMP synchronization primitive.
    struct WaitQueue {
        void *head;
        void *tail;
        volatile uint32_t lock;
        // Appended field (do not reorder head/tail/lock above). Singly-linked
        // list head of per-thread poll nodes for threads registered through
        // wait_queue_wait_any(). It is null for a queue that has never had a
        // multi-queue waiter and stays independent of the single-waiter head/tail
        // chain so wait_queue_wait_until()/wake_* single-queue semantics are
        // unchanged.
        void *poll_head;
    };

    // Layout guard: the appended poll_head MUST stay after the existing fields so
    // statically-initialized WaitQueue storage in each backend keeps its meaning.
    static_assert(__builtin_offsetof(WaitQueue, head) == 0, "WaitQueue head slot moved");
    static_assert(__builtin_offsetof(WaitQueue, tail) == sizeof(void *), "WaitQueue tail slot moved");
    static_assert(__builtin_offsetof(WaitQueue, lock) == 2 * sizeof(void *), "WaitQueue lock slot moved");
    static_assert(
        __builtin_offsetof(WaitQueue, poll_head) > __builtin_offsetof(WaitQueue, lock),
        "WaitQueue poll_head must be appended last");

    // Create a kernel thread.
    //
    // Non-interrupt-context only. This path allocates a thread control block and
    // a kernel stack through the ordinary phase 3 allocator contract, so it MUST
    // NOT be called from any IRQ handler, the timer on_tick() path, irq_dispatch,
    // or any CPU exception handler. The default placement prefers the calling CPU
    // and may fall back to the shortest online/schedulable CPU-owned run queue.
    //
    // Returns the new thread id on success, or INVALID_THREAD_ID on failure.
    // The created thread starts in the Runnable state.
    ThreadId create_kernel_thread(ThreadEntry __entry, void *__arg) noexcept;
    ThreadId create_kernel_thread_on_cpu(ThreadEntry __entry, void *__arg, cpu::CpuId __target_cpu) noexcept;

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

    // Associate (or clear) the calling thread's user process. The scheduler
    // re-establishes the proc layer's global ring3 context (current process,
    // user CR3, TSS rsp0) from this value whenever the thread is switched back
    // in. Pass nullptr for pure kernel threads. The argument is an opaque
    // bigos::proc::Process* to avoid a scheduler dependency on the proc layout.
    void set_current_user_process(void *__process) noexcept;

    // Initialize the calling CPU's scheduler domain. AP startup uses this before
    // publishing the AP as schedulable; BSP-only configurations lazily initialize
    // the bootstrap domain before the first thread is created.
    bool init_current_cpu_domain() noexcept;

    // Enter the scheduler. Non-interrupt-context only.
    //
    // Adopts the current boot/main execution context as a scheduler thread,
    // registers the scheduler-owned idle thread, and begins running the calling
    // CPU's run queue. Must be called with maskable interrupts enabled so a timer
    // or scheduler nudge can wake the idle thread's hlt. This does not return to
    // its caller in the normal sense; the calling context becomes that CPU's idle
    // thread.
    void start() noexcept;

    // Context guard used by blocking APIs. Blocking is allowed only from ordinary
    // running kernel-thread context after sched::start(), with maskable IRQs
    // enabled and outside IRQ/exception/syscall/fatal/scheduler critical paths.
    bool can_block() noexcept;
    // Allocation-safe predicate for the CPU page-fault materialization path
    // (demand-zero / COW split). Unlike can_block() this does NOT require a
    // blockable context: a real ring3 #PF dispatches under the nonblocking guard
    // with IF=0, yet frame allocation there does not block. It requires only an
    // ordinary running kernel-thread context after sched::start() (not the idle
    // thread) and that no scheduler critical section is held. The fault handler
    // never blocks, so this is sufficient and strictly weaker than can_block().
    bool can_allocate_in_fault() noexcept;
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

    // Multi-queue blocking wait. The current non-interrupt thread first checks
    // __predicate under the scheduler critical section; if it is already true no
    // registration happens and WAIT_OK is returned. Otherwise the thread is
    // registered on the first __count wait queues (bounded by
    // POLL_MAX_WAIT_QUEUES) and blocked until any one of them is woken through
    // wake_one()/wake_all(), or the optional monotonic-tick timeout expires.
    // timeout_ticks == 0 means no timeout. It returns WAIT_OK on a queue wakeup,
    // WAIT_TIMEOUT on deadline expiry, WAIT_INVALID for bad arguments, and
    // WAIT_BLOCK_FORBIDDEN when the context may not block. Registration nodes come
    // from the thread's own stable storage, so this path never allocates. Duplicate
    // queue pointers in the array are tolerated (registered once each; a woken
    // thread self-removes from every queue it registered on). This does not change
    // single-queue wait_queue_wait_until()/wake_* semantics.
    int wait_queue_wait_any(WaitQueue **__queues, uint32_t __count, WaitPredicate __predicate, void *__arg,
                            timer::tick_t __timeout_ticks = 0) noexcept;

    // Allocation-free wakeups. IRQ handlers may call these bounded helpers. They
    // make waiters runnable on the waiter-owned scheduler domain and may request
    // a scheduler nudge when the target CPU is remote. The nudge is scoped only
    // to reschedule observation, not generic IPI or TLB shootdown semantics.
    uint32_t wake_one(WaitQueue *__queue) noexcept;
    uint32_t wake_all(WaitQueue *__queue) noexcept;

    // Cooperative scheduler sleep. The current thread becomes non-runnable until
    // the monotonic tick deadline expires. It returns WAIT_TIMEOUT on expiry.
    int sleep_for(timer::tick_t __ticks) noexcept;

    // IRQ-context-safe bounded scheduler tick hook.
    //
    // Called from each valid CPU-local timer path after the timer owner updates
    // any global tick source. It performs bounded time-slice accounting, records
    // CPU-local reschedule intent, and wakes that CPU's sleepers. It MUST NOT
    // allocate, free, block, do bulk IO, or switch threads directly.
    void on_timer_tick() noexcept;

    // Scheduler-owned IPI/nudge ISR hook. It only asks the current CPU to
    // observe its run queue at the normal IRQ-return scheduling boundary.
    void on_scheduler_nudge() noexcept;

    // External IRQ-return bridge. irq_dispatch calls this only after the
    // registered handler completes and the single i8259 EOI has been sent.
    // Exceptions and int 0x80 syscalls do not enter this path.
    void maybe_preempt_on_irq_return(irq::InterruptFrame *__frame) noexcept;
}   // namespace sched
NAMESPACE_BIGOS_END

#endif   // _BIG_SCHED_H
