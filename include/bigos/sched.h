#ifndef _BIG_SCHED_H
#define _BIG_SCHED_H

#include <bigos/types.h>
#include <bigos/thread.h>

NAMESPACE_BIGOS_BEG
namespace sched {
    // Invalid/unassigned thread id sentinel.
    constexpr ThreadId INVALID_THREAD_ID = 0;

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
    // running (or the idle thread runs) without corrupting the run queue. Stage 4
    // performs NO IRQ-return preemption: switching only happens here.
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

    // IRQ-context-safe bounded scheduler tick hook.
    //
    // Called from the timer IRQ0 path after on_tick(). It only records a bounded
    // reschedule intent for later cooperative/future scheduling. It MUST NOT
    // allocate, free, block, do IO, or switch threads. Stage 4 never preempts on
    // IRQ return.
    void on_timer_tick() noexcept;
}   // namespace sched
NAMESPACE_BIGOS_END

#endif   // _BIG_SCHED_H
