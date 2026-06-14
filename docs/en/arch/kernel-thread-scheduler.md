# Kernel Threads And Single-Core Scheduler

BigOS stage 11 keeps the minimal kernel-thread model single-core, but extends the scheduler from purely cooperative round-robin to bounded timer-driven semantics. Ordinary kernel threads still use the same TCB, run queue, idle ownership, and cooperative switch frame; PIT IRQ0 may now expire a time slice and request a guarded IRQ-return switch.

## Scope

- Define a kernel thread control block (TCB): thread ID, lifecycle state, saved stack pointer, kernel stack range, entry function and argument, intrusive run queue node, intrusive wait/sleep nodes, and terminated-list node.
- Provide a scheduler-facing kernel context-switch boundary through
  `bigos::arch_context::switch_kernel_context()`. The current implementation
  enters the x86_64 `switch_context` assembly primitive, which saves/restores
  System V callee-saved registers (`rbp`, `rbx`, `r12`-`r15`) and stack pointer.
- Provide single-core round-robin `yield()`, `sched::start()` entry, and scheduler-owned idle thread.
- Replace the bare `hlt` loop at the end of `kernel()` with the idle thread.
- Let timer IRQ0 account ordinary thread time slices, record reschedule intent, and wake expired sleepers through bounded, IRQ-context-safe `sched::on_timer_tick()`.
- Allow external IRQ return to switch threads only after the registered handler finishes, the single i8259 EOI is sent, and scheduler preemption guards approve the boundary.

## Non-Goals And Boundaries

- **Single-core**: no SMP balancing, per-CPU run queues, IPI, affinity, or cross-CPU synchronization.
- **No full priority scheduler**: stage 11 records bounded priority/policy metadata only. Default selection remains deterministic single-core round-robin.
- **Bounded process and syscall integration**: timer preemption does not create
  user-visible POSIX scheduling policy. Later process/fd/VFS syscalls may use
  `sched::can_block()` from ordinary process syscall contexts, but only inside
  the same single-core blocking contract.
- **Stage 11 blocking boundary**: wait queues and tick-based sleep remain
  single-core kernel-thread primitives. There is still no CFS, real-time
  scheduling, POSIX blocking IO policy, SMP, or user-visible cancellation and
  signal semantics.
- Does not change fixed boot addresses, higher-half base, kernel load base, BootInfo ABI, direct map, `KVMEM_BASE`, IDT vector allocation, or `InterruptFrame` ABI.

## Thread Model And State

Each kernel thread is represented by a TCB that records a stable thread ID, `ThreadState`, saved stack pointer (cooperative context pointer), kernel stack base/size, entry function, argument, bounded time-slice state, and reserved priority/policy metadata. `ThreadState` includes `Runnable`, `Running`, `Idle`, `Blocked`, `Sleeping`, and `Terminated`. `Blocked` and `Sleeping` are bounded non-runnable kernel wait states only; they do not imply POSIX blocking IO, user wait queues, process ownership, cancellation policy, or SMP migration.

The run queue, wait queues, sleep list, and terminated list are intrusive lists. Their nodes (`rq_next`, `wait_next`, `sleep_next`, `term_next`) are owned by the TCB itself and share its lifetime. The scheduler path therefore does not depend on ordinary heap containers and does not allocate queue nodes in IRQ handlers. A thread may belong to at most one explicit wait queue and one timeout tracking list at a time.

## Blocking Context Contract

`sched::can_block()` permits blocking only after `sched::start()` from an ordinary running kernel thread with maskable IRQs enabled, outside IRQ/exception/syscall dispatch, fatal diagnostics, and scheduler critical sections. Blocking APIs return deterministic negative wait errors when this contract is violated; they do not silently busy-wait or enqueue the current thread from a forbidden context.

`sched::enter_nonblocking_context()` / `sched::leave_nonblocking_context()` mark forbidden dispatch regions as nonblocking. The guard protects timer IRQ0, keyboard IRQ1, CPU exceptions, and non-process or otherwise unsupported `int 0x80` contexts from calling wait APIs. `sched::disable_preemption()` / `sched::enable_preemption()` provide a separate bounded timer-preemption guard; scheduler critical sections use that guard so run queue, wait queue, sleep list, state transitions, and switch preparation cannot be interrupted by IRQ-return preemption. Ordinary process syscalls that preserve IF and run after scheduler start may pass `sched::can_block()`; unsupported contexts remain deterministic nonblocking error paths.

## Wait Queues And Sleep

`sched::WaitQueue` is a small owner-supplied head/tail object whose members point at scheduler-owned TCBs. `sched::wait_queue_wait_until()` checks an optional predicate with IRQs disabled, records queue membership before transitioning the current thread to `Blocked` or `Sleeping`, and switches cooperatively to another runnable thread or the idle thread. This predicate check avoids a missed wakeup between an empty-buffer check and enqueue.

`sched::wake_one()` and `sched::wake_all()` are allocation-free and may be called from bounded IRQ-safe producer paths. Wakeup removes the selected waiter from its wait queue and timeout tracking exactly once, changes it back to `Runnable`, and appends it to the run queue for a later cooperative or guarded IRQ-return scheduling point.

`sched::sleep_for()` and `timer::sleep_for()` use the existing monotonic PIT tick to block the current thread until a deadline expires. Expired sleepers are scanned by `sched::on_timer_tick()` through a bounded intrusive list; the IRQ path does not allocate, free, block, print bulk output, access filesystem services, or switch threads.

## Allocator Context Contract

`create_kernel_thread()` may be called only from non-interrupt context. It allocates resources through the stage 3 ordinary allocator contract (`kmalloc()` for TCB, `alloc_kernel_pages()` for stack). Timer IRQ0, keyboard IRQ1, `#PF`, and `irq_dispatch` paths do not create or free thread objects and do not perform ordinary dynamic allocation. Failure paths release only resources allocated by the same non-interrupt-context call path and do not violate the allocator contract.

Stage 4 ordinary kernel threads use a fixed 1-page kernel stack by default, and the TCB records the stack base/size. This default page count is not exposed as a smoke/debug build option. If larger stacks or guard pages become necessary, they should be introduced with stack-overflow diagnostics in a separate change.

## Context Switch

`bigos::arch_context::switch_kernel_context()` is the scheduler-facing context
primitive. It treats saved stack pointer values as opaque scheduler-owned kernel
context tokens and enters `switch_context(uint64_t *old_sp, uint64_t new_sp)`
(`kernel/core/sched/switch.s`) in the current x86_64 backend. The assembly saves
the current thread's callee-saved registers and stack pointer into `*old_sp`,
loads the target stack, and `ret`s into its saved return point. Cooperative
`yield()`/`thread_exit()` and IRQ-return preemption use this same boundary rather
than open-coding assembly frame offsets. IRQ-return preemption uses a
scheduler-owned bridge after EOI; the bridge saves the interrupted thread's
current kernel stack continuation and later resumes it so the original
`InterruptFrame` is restored by `isr_common` before `iretq`.

`create_kernel_thread()` preconstructs a new thread stack. The first time it is scheduled, `switch_context` returns into scheduler-owned `thread_trampoline`; the trampoline enables interrupts and calls the thread entry. If the entry returns, control enters `thread_exit()`.

Callers execute `cli` before `switch_context`, and each resume point executes `sti` after the switch returns, keeping the switch critical section from being interrupted by IRQs.

## Scheduling Policy And Idle

`yield()` is a single-core round-robin cooperative switch. If another runnable thread exists, the current thread is pushed to the tail of the run queue and the next runnable thread is selected. A selected ordinary thread receives a fixed bounded time slice. Blocked, sleeping, idle, and terminated threads are not eligible for normal runnable selection. If no other runnable thread exists, the current thread or idle keeps running without corrupting the run queue.

`sched::start()` adopts the boot/main execution context as the scheduler-owned idle thread, reusing the existing boot stack. It then enters an idle loop: first `yield()` to runnable threads, otherwise execute `hlt` and wait for an IRQ before reevaluating. Idle `hlt` must run with IRQs enabled, so `start()` is called after `irq::enableIRQ()`, allowing timer IRQ0 to wake the CPU.

## Thread Exit And Deferred Reclamation

`thread_exit()` removes any stale wait/sleep membership, marks the current thread `Terminated`, removes it from runnable scheduling, and links it into the scheduler-owned terminated list. It **does not** immediately free the current thread's TCB or kernel stack on the exiting stack. Safe reclamation is deferred to a later lifecycle change; this stage only guarantees terminated threads are never reinserted into the runnable queue and the stage 4 thread count is bounded.

## Timer And IRQ Boundary

The timer IRQ0 handler continues to advance ticks through `bigos::timer::on_tick()`, then calls bounded IRQ-context-safe `bigos::sched::on_timer_tick()`. The scheduler hook decrements the current ordinary thread's time slice, records pending reschedule intent on expiry, and wakes expired sleepers through allocation-free intrusive state; it does not allocate, free, block, print bulk output, or switch directly from the timer handler.

External IRQ dispatch keeps centralized EOI ownership. After a registered
handler returns, `irq_dispatch` sends exactly one i8259 EOI, then calls
`sched::maybe_preempt_on_irq_return()`. That bridge switches only when
preemption is enabled, `bigos::arch_context` reports a kernel IRQ-return
context, the current thread is still ordinary/running, and a runnable peer
exists. CPU exceptions and `int 0x80` syscalls never enter the bridge, send no
i8259 EOI, and remain outside sleep/process-lifecycle recovery paths.

## Validation Smoke

`scheduler_smoke` is default off. When `BIGOS_SCHEDULER_SMOKE` is enabled, `kernel()` creates two worker threads. Each emits fixed-count `BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B` markers and calls `yield()`, proving two kernel threads can switch cooperatively. Ordinary boot does not create smoke threads and does not emit scheduler markers.

`blocking_smoke` is also default off. When `BIGOS_BLOCKING_SMOKE` is enabled, `kernel()` creates a blocking reader and a synthetic TTY producer. The smoke emits `BIGOS_BLOCKING_WAIT_BLOCKED`, `BIGOS_BLOCKING_WAKE_SENT`, `BIGOS_BLOCKING_WAIT_RESUMED`, `BIGOS_BLOCKING_TIMEOUT_BLOCKED`, `BIGOS_BLOCKING_TIMEOUT_EXPIRED`, and `BIGOS_BLOCKING_SMOKE_PASSED` to validate wait queue wakeup, timeout sleep, and cooperative resume without manual keyboard input.

`scheduler_semantics_smoke` is default off. When `BIGOS_SCHEDULER_SEMANTICS_SMOKE` is enabled, `kernel()` creates two ordinary kernel threads. The first observes `BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED` while preemption is disabled and timer ticks continue; the second can emit `BIGOS_SCHED_SEMANTICS_PREEMPTED` and `BIGOS_SCHED_SEMANTICS_PASSED` only if timer-driven IRQ-return preemption runs it. These markers are distinct from the cooperative `scheduler_smoke` markers.
