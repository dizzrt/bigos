# Kernel Threads And Single-Core Scheduler

BigOS stage 4 introduces a minimal kernel-thread model and single-core round-robin scheduler, moving the system from one initialization path into an early runtime that can cooperatively switch among multiple kernel threads. The facility targets only x86_64 single-core, Legacy BIOS, i8259, PIT IRQ0, and Bochs validation scenarios.

## Scope

- Define a kernel thread control block (TCB): thread ID, lifecycle state, saved stack pointer, kernel stack range, entry function and argument, intrusive run queue node, and terminated-list node.
- Provide an x86_64 cooperative context switch (`switch_context`) that saves/restores System V callee-saved registers (`rbp`, `rbx`, `r12`-`r15`) and stack pointer.
- Provide single-core round-robin `yield()`, `sched::start()` entry, and scheduler-owned idle thread.
- Replace the bare `hlt` loop at the end of `kernel()` with the idle thread.
- Let timer IRQ0 only record reschedule intent through bounded, IRQ-context-safe `sched::on_timer_tick()`.

## Non-Goals And Boundaries

- **Single-core**: no SMP balancing, per-CPU run queues, IPI, affinity, or cross-CPU synchronization.
- **No user mode**: no ring3 transition, process model, syscall ABI, address-space switch, or user-program loading.
- **No blocking sleep**: no priority scheduling, CFS, real-time scheduling, wait queues, sleep queues, blocking IO, or scheduler sleep. Thread states are only runnable, running, idle, or terminated.
- Does not change fixed boot addresses, higher-half base, kernel load base, BootInfo ABI, direct map, `KVMEM_BASE`, IDT vector allocation, or `InterruptFrame` ABI.

## Thread Model And State

Each kernel thread is represented by a TCB that records a stable thread ID, `ThreadState`, saved stack pointer (cooperative context pointer), kernel stack base/size, entry function, and argument. `ThreadState` is limited to `Runnable`, `Running`, `Idle`, and `Terminated`; no state implies blocking IO, user wait queues, process ownership, or SMP migration.

Both the run queue and terminated list are intrusive lists. Their nodes (`rq_next`, `term_next`) are owned by the TCB itself and share its lifetime. The scheduler path therefore does not depend on ordinary heap containers and does not need to allocate queue nodes in IRQ handlers.

## Allocator Context Contract

`create_kernel_thread()` may be called only from non-interrupt context. It allocates resources through the stage 3 ordinary allocator contract (`kmalloc()` for TCB, `alloc_kernel_pages()` for stack). Timer IRQ0, keyboard IRQ1, `#PF`, and `irq_dispatch` paths do not create or free thread objects and do not perform ordinary dynamic allocation. Failure paths release only resources allocated by the same non-interrupt-context call path and do not violate the allocator contract.

Stage 4 ordinary kernel threads use a fixed 1-page kernel stack by default, and the TCB records the stack base/size. This default page count is not exposed as a smoke/debug build option. If larger stacks or guard pages become necessary, they should be introduced with stack-overflow diagnostics in a separate change.

## Context Switch

`switch_context(uint64_t *old_sp, uint64_t new_sp)` (`src/kernel/sched/switch.s`) saves the current thread's callee-saved registers and stack pointer into `*old_sp`, loads the target stack, and `ret`s into its saved return point. It does not change `InterruptFrame` layout, generated ISR entry frames, EOI/`iretq` behavior, and is used only for non-interrupt-context cooperative `yield()`/`thread_exit()`.

`create_kernel_thread()` preconstructs a new thread stack. The first time it is scheduled, `switch_context` returns into scheduler-owned `thread_trampoline`; the trampoline enables interrupts and calls the thread entry. If the entry returns, control enters `thread_exit()`.

Callers execute `cli` before `switch_context`, and each resume point executes `sti` after the switch returns, keeping the switch critical section from being interrupted by IRQs.

## Scheduling Policy And Idle

`yield()` is a single-core round-robin cooperative switch. If another runnable thread exists, the current thread is pushed to the tail of the run queue and the next runnable thread is selected. If no other runnable thread exists, the current thread or idle keeps running without corrupting the run queue.

`sched::start()` adopts the boot/main execution context as the scheduler-owned idle thread, reusing the existing boot stack. It then enters an idle loop: first `yield()` to runnable threads, otherwise execute `hlt` and wait for an IRQ before reevaluating. Idle `hlt` must run with IRQs enabled, so `start()` is called after `irq::enableIRQ()`, allowing timer IRQ0 to wake the CPU.

## Thread Exit And Deferred Reclamation

`thread_exit()` marks the current thread `Terminated`, removes it from runnable scheduling, and links it into the scheduler-owned terminated list. It **does not** immediately free the current thread's TCB or kernel stack on the exiting stack. Safe reclamation is deferred to a later lifecycle change; this stage only guarantees terminated threads are never reinserted into the runnable queue and the stage 4 thread count is bounded.

## Timer And IRQ Boundary

The timer IRQ0 handler continues to advance ticks through `bigos::timer::on_tick()`, then calls bounded IRQ-context-safe `bigos::sched::on_timer_tick()`. The latter only increments a reschedule-intent counter; it does not allocate, free, block, perform IO, or switch threads. Stage 4 does not preempt on IRQ return. Thread switches happen only in non-interrupt context through `yield()`/`thread_exit()`. IRQ dispatch keeps the existing EOI, saved-frame, register-restore, and `iretq` semantics. CPU exceptions remain diagnostic-only and are not wired into thread recovery, wakeup, or retry.

## Validation Smoke

`scheduler_smoke` is default off. When `BIGOS_SCHEDULER_SMOKE` is enabled, `kernel()` creates two worker threads. Each emits fixed-count `BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B` markers and calls `yield()`, proving two kernel threads can switch cooperatively. Ordinary boot does not create smoke threads and does not emit scheduler markers.
