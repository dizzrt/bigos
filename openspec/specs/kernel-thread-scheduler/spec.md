## Purpose

Define the BigOS minimal single-core kernel thread and scheduler foundation: explicit kernel thread runtime state, memory-context-safe thread creation, an x86_64 kernel context switch primitive, a single-core round-robin scheduler with cooperative yield, deferred reclamation of terminated threads, a scheduler-owned idle thread that replaces the naked `kernel()` halt loop, and deterministic scheduler smoke validation. This foundation excludes SMP balancing, preemptive IRQ-return scheduling, processes, user mode, syscalls, blocking IO, and filesystem services.

## Requirements

### Requirement: Kernel threads have explicit runtime state

BigOS SHALL provide a minimal single-core kernel thread abstraction with explicit thread identity, kernel stack ownership, saved execution context, and lifecycle state.

#### Scenario: Thread control block records execution state

- **WHEN** a kernel thread is created
- **THEN** its thread control block MUST record a stable thread ID, stack range, saved stack pointer or equivalent context pointer, entry function, argument, and lifecycle state
- **AND** the thread control block MUST NOT require hosted C++ runtime services, exceptions, RTTI, files, sockets, or user-mode services

#### Scenario: Thread states are bounded

- **WHEN** the scheduler evaluates a thread
- **THEN** the thread state MUST be one of a bounded early-kernel set such as runnable, running, idle, or terminated
- **AND** no state MUST imply blocking IO, user wait queues, process ownership, or SMP migration

### Requirement: Kernel thread creation obeys memory context contracts

BigOS SHALL create and destroy kernel thread metadata and stacks only from non-interrupt context under the current single-core early-kernel allocator contract.

#### Scenario: Thread creation occurs outside IRQ handler

- **WHEN** kernel code creates a kernel thread
- **THEN** the creation path MUST run in non-interrupt context
- **AND** it MAY use ordinary allocator APIs such as `kmalloc()` or `alloc_kernel_pages()` only under the phase 3 allocator contract

#### Scenario: IRQ handlers do not allocate scheduler objects

- **WHEN** timer IRQ0, keyboard IRQ1, `#PF`, or external IRQ dispatch code runs
- **THEN** the path MUST NOT allocate or free thread control blocks, kernel stacks, run-queue nodes, or other scheduler-owned dynamic objects through ordinary allocator APIs

#### Scenario: Default kernel stack is one page

- **WHEN** a normal stage 4 kernel thread is created
- **THEN** BigOS MUST allocate a fixed one-page kernel stack for that thread by default
- **AND** stage 4 MUST NOT expose a smoke/debug build switch that changes the default kernel thread stack page count

### Requirement: Context switch preserves kernel execution context

BigOS SHALL provide an x86_64 kernel context switch primitive that transfers execution between kernel threads while preserving the required callee-saved register and stack state.

#### Scenario: Existing thread context is saved

- **WHEN** the scheduler switches away from a running kernel thread
- **THEN** the context switch primitive MUST save enough state for that thread to resume at the switch return point
- **AND** the saved state MUST include the stack pointer and the callee-saved register set required by the selected x86_64 calling convention boundary

#### Scenario: New thread enters trampoline

- **WHEN** a newly created kernel thread is scheduled for the first time
- **THEN** its prepared stack/context MUST enter a scheduler-owned trampoline or equivalent startup path
- **AND** the trampoline MUST call the thread entry function with its argument and handle entry return through a controlled `thread_exit()` or fatal diagnostic path

#### Scenario: Interrupt frame ABI remains unchanged

- **WHEN** kernel context switching is added
- **THEN** BigOS MUST NOT change the existing `InterruptFrame` layout, generated ISR entry frame layout, or CPU exception versus external IRQ dispatch ABI solely to support cooperative thread switching

### Requirement: Scheduler uses single-core round-robin policy

BigOS SHALL provide a minimal single-core round-robin scheduler that selects runnable kernel threads from a bounded run queue, rotates the current thread through explicit cooperative yield, and may preempt an eligible running thread on a timer-driven IRQ-return boundary after its time slice expires.

#### Scenario: Yield switches to another runnable thread

- **WHEN** a running kernel thread calls `yield()` and at least one other runnable non-idle thread exists
- **THEN** the scheduler MUST place the current thread back on the runnable queue
- **AND** the scheduler MUST switch to the next runnable thread in round-robin order

#### Scenario: Yield returns when no peer is runnable

- **WHEN** a running kernel thread calls `yield()` and no other non-idle runnable thread exists
- **THEN** the scheduler MUST continue running the current thread or switch to idle without corrupting the run queue

#### Scenario: Scheduler remains single-core

- **WHEN** scheduler APIs or documentation describe scheduling behavior
- **THEN** they MUST state that the scheduler is single-core only and does not provide SMP balancing, IPI, per-CPU run queues, cross-CPU synchronization, or TLB shootdown

#### Scenario: Timer slice expiry requests preemption

- **WHEN** timer IRQ0 observes that the current ordinary runnable thread has consumed its configured time slice
- **THEN** BigOS MUST record a bounded reschedule intent without ordinary dynamic allocation
- **AND** the scheduler MAY switch away from the current thread only at a documented IRQ-return scheduling boundary where preemption is enabled

#### Scenario: Preemption preserves cooperative scheduling

- **WHEN** no timer-driven preemption is pending or preemption is disabled
- **THEN** explicit `yield()`, blocking wait, timeout wakeup, and idle scheduling MUST continue to use the existing scheduler state machine
- **AND** runnable queue order MUST remain deterministic under the single-core round-robin policy

### Requirement: Terminated threads are not immediately reclaimed

BigOS SHALL avoid immediate current-thread TCB or stack reclamation during the stage 4 thread exit path.

#### Scenario: Thread exit defers reclamation

- **WHEN** a kernel thread entry function returns or calls `thread_exit()`
- **THEN** BigOS MUST mark the thread terminated and remove it from runnable scheduling
- **AND** BigOS MUST NOT immediately free the current thread's TCB or kernel stack on that same exit stack

#### Scenario: Terminated list is bounded by stage 4 scope

- **WHEN** a thread becomes terminated in stage 4
- **THEN** BigOS MUST keep it in a scheduler-owned terminated list or equivalent non-runnable tracking structure
- **AND** documentation or validation notes MUST record that safe reclamation is deferred to a later lifecycle change

### Requirement: Idle thread owns halt behavior

BigOS SHALL replace the post-initialization naked `kernel()` halt loop with a scheduler-owned idle thread.

#### Scenario: Kernel enters scheduler after initialization

- **WHEN** `kernel()` completes memory, TTY, IRQ, timer, and diagnostic initialization
- **THEN** it MUST enter the scheduler start path rather than directly ending in an unmanaged infinite `hlt` loop

#### Scenario: Idle halts only when no runnable work exists

- **WHEN** no non-idle kernel thread is runnable
- **THEN** the scheduler MUST run the idle thread
- **AND** the idle thread MAY execute `hlt` only under documented interrupt-readiness assumptions that allow timer IRQs to wake the CPU

### Requirement: Scheduler smoke is deterministic

BigOS SHALL provide validation evidence that at least two kernel threads can run and yield in a deterministic bounded sequence.

#### Scenario: Two worker threads emit alternating markers

- **WHEN** scheduler smoke is enabled for validation
- **THEN** BigOS MUST create at least two kernel worker threads
- **AND** validation MUST be able to observe a bounded deterministic marker sequence showing both threads ran and yielded or were rescheduled

#### Scenario: Source checks cover scheduler invariants

- **WHEN** this change is implemented
- **THEN** source-level tests MUST cover thread creation context annotations, context switch symbol presence, idle-thread replacement of the naked `kernel()` halt loop, IRQ handler allocator exclusions, and scheduler smoke marker wiring

#### Scenario: Build and emulator validation are recorded

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` or cross-toolchain build, relevant `uv run pytest` source checks, and `openspec validate introduce-kernel-threads-scheduler --strict`
- **AND** if Bochs runtime smoke cannot observe scheduler markers due to emulator, ROM, serial oracle, image lock, or interactive limitations, validation MUST record the unavailable dependency and remaining scheduler bootability risk

### Requirement: Scheduler skips blocked and sleeping threads
BigOS SHALL extend the single-core cooperative scheduler so blocked and sleeping threads are non-runnable until a wakeup, timeout, cancellation, or termination transition makes them runnable again.

#### Scenario: Yield does not schedule blocked thread
- **WHEN** the scheduler selects the next thread after `yield()` or an explicit scheduling point
- **THEN** it MUST skip threads whose state is blocked, sleeping, or equivalent non-runnable wait state
- **AND** it MUST select another runnable thread or the idle thread without corrupting run-queue order

#### Scenario: Wakeup returns thread to runnable queue
- **WHEN** a blocked or sleeping thread is woken by a wait queue wakeup or timeout
- **THEN** the scheduler MUST make that thread runnable exactly once
- **AND** the thread MUST become eligible for a later cooperative scheduling point

#### Scenario: Stage 10 remains non-preemptive
- **WHEN** a thread becomes runnable because of wakeup or timeout
- **THEN** BigOS MUST NOT perform timer-driven IRQ-return context switching in this stage
- **AND** the existing interrupt frame ABI, context-switch frame layout, EOI ordering, and idle-thread ownership MUST remain unchanged

### Requirement: Scheduler exposes blocking context rules
BigOS SHALL provide scheduler-facing helpers or documented rules that allow kernel code to determine whether the current context may block and whether timer-driven preemption may switch away from the current thread.

#### Scenario: Blocking is allowed only from ordinary thread context
- **WHEN** a blocking wait or sleep API checks the current context
- **THEN** it MUST succeed only for ordinary non-interrupt thread context with scheduler state initialized and no active scheduler critical section
- **AND** it MUST reject IRQ handlers, CPU exception handlers, fatal diagnostics, and contexts where interrupts are disabled in a way that would prevent timer progress

#### Scenario: Scheduler critical section prevents sleep
- **WHEN** kernel code owns scheduler run-queue or wait-queue critical state
- **THEN** blocking wait APIs MUST NOT schedule away the current thread from inside that critical section
- **AND** source-level checks or validation notes MUST cover this restriction

#### Scenario: Preemption disable protects scheduler state
- **WHEN** kernel code enters a scheduler critical section, wait-queue mutation, sleep-list mutation, context-switch preparation, or another documented non-preemptible region
- **THEN** BigOS MUST disable timer-driven preemption for that region through a bounded guard or equivalent state
- **AND** timer IRQ0 MAY record pending reschedule intent but MUST NOT switch away from the protected region on IRQ return

#### Scenario: Preemption enable handles pending reschedule
- **WHEN** the outermost preemption-disable guard exits and a pending reschedule intent exists for a still-runnable current thread
- **THEN** BigOS MUST process the pending intent at a deterministic scheduler boundary or preserve it for the next safe IRQ-return boundary
- **AND** it MUST NOT silently lose the pending reschedule state

### Requirement: Scheduler accounts time slices
BigOS SHALL track a bounded per-thread or per-current-thread time slice under the single-core scheduler and use PIT ticks to decide when an eligible running thread should be rescheduled.

#### Scenario: Runnable thread receives a time slice
- **WHEN** the scheduler selects an ordinary runnable non-idle thread
- **THEN** BigOS MUST assign or refresh a bounded time slice for that thread
- **AND** the time slice state MUST NOT require ordinary dynamic allocation during timer IRQ handling

#### Scenario: Timer tick consumes current slice
- **WHEN** PIT IRQ0 runs while an ordinary runnable thread is current and preemption accounting is enabled
- **THEN** BigOS MUST decrement or otherwise account that thread's time slice through an IRQ-safe scheduler hook
- **AND** the hook MUST NOT allocate, free, block, call `mdelay()`, access filesystem services, depend on user-mode services, or perform bulk console/serial output

#### Scenario: Idle thread is not pointlessly preempted
- **WHEN** the idle thread is current and no non-idle runnable thread exists
- **THEN** timer tick accounting MUST NOT force a meaningless preemptive switch away from idle
- **AND** idle MUST remain scheduler-owned and may halt only under documented interrupt-readiness assumptions

### Requirement: Scheduler supports priority extension hooks
BigOS SHALL provide a minimal bounded priority hook, static priority field, or reserved scheduler policy slot without requiring a complete priority scheduler in stage 11.

#### Scenario: Default policy remains round-robin
- **WHEN** no explicit priority policy is configured or implemented beyond the hook
- **THEN** runnable non-idle threads MUST continue to be selected using the deterministic single-core round-robin policy
- **AND** the hook MUST NOT introduce SMP, dynamic allocation in IRQ context, or user-visible POSIX scheduling policy

#### Scenario: Priority metadata is bounded
- **WHEN** a thread records priority metadata or a scheduler policy hook result
- **THEN** the value MUST be stored in scheduler-owned bounded state
- **AND** source-level checks or validation notes MUST document that full priority scheduling, fairness policy, and real-time semantics are deferred

### Requirement: Preemptive scheduler smoke is deterministic
BigOS SHALL provide validation evidence that timer-driven preemption works without breaking cooperative yield, blocking states, idle ownership, or interrupt return invariants.

#### Scenario: Time slice smoke observes preemption
- **WHEN** scheduler semantics smoke is enabled for validation
- **THEN** BigOS MUST run at least two runnable kernel threads where one thread can be preempted after a bounded time slice
- **AND** validation MUST observe fixed serial markers showing that timer-driven rescheduling occurred

#### Scenario: Preemption disable smoke delays switch
- **WHEN** scheduler semantics smoke enters a documented preemption-disabled region while timer ticks continue
- **THEN** validation MUST observe that reschedule intent is delayed until the region exits
- **AND** it MUST observe that the pending reschedule is later processed or explicitly recorded as still pending at a safe boundary

#### Scenario: Blocked and sleeping threads remain non-runnable
- **WHEN** timer-driven preemption selects the next thread after a slice expires
- **THEN** the scheduler MUST skip blocked, sleeping, terminated, or otherwise non-runnable threads
- **AND** it MUST select another runnable thread or the idle thread without corrupting wait queue or run queue state

### Requirement: Scheduler consumes context boundary semantics

BigOS scheduler SHALL consume context-switch and interrupt-return semantics through documented scheduler-facing boundaries rather than depending on raw x86_64 ISR frame or assembly implementation details.

#### Scenario: Scheduler requests switch through context primitive

- **WHEN** the scheduler selects another runnable kernel thread
- **THEN** it MUST transfer control through the documented kernel context-switch primitive or a scheduler-owned wrapper around it
- **AND** ordinary scheduler policy MUST NOT open-code callee-saved register layout, stack-frame construction, or architecture-private assembly offsets outside the context boundary

#### Scenario: IRQ-return preemption uses eligibility checks

- **WHEN** timer-driven preemption intent is processed at an IRQ-return boundary
- **THEN** the scheduler MUST verify preemption state, current thread eligibility, run-queue state, and non-preemptible region guards before switching
- **AND** the switch MUST be deferred when the interrupted path cannot safely preserve scheduler and interrupt frame semantics

### Requirement: Scheduler critical regions protect boundary invariants

BigOS scheduler SHALL protect run-queue, wait-queue, sleep-list, and context-switch preparation regions from timer-driven preemption that would violate single-core invariants.

#### Scenario: Protected region delays preemption

- **WHEN** timer IRQ0 records reschedule intent while the current thread is inside a scheduler critical section or another documented preemption-disabled region
- **THEN** BigOS MUST preserve the pending intent or equivalent bounded state
- **AND** it MUST NOT switch away from the protected region until a deterministic safe scheduler boundary is reached

#### Scenario: Scheduler remains allocation-free from IRQ path

- **WHEN** scheduler-facing code runs from timer IRQ or external IRQ dispatch context
- **THEN** it MUST NOT allocate or free thread control blocks, kernel stacks, run-queue nodes, wait-queue nodes, or other ordinary scheduler-owned dynamic objects
- **AND** any required scheduler object lifetime MUST be established from non-interrupt context before the IRQ path consumes it

### Requirement: Scheduler boundary validation preserves single-core semantics

BigOS SHALL validate scheduler/context boundary changes without expanding the scheduler scope beyond the current single-core model.

#### Scenario: Source checks cover context boundary assumptions

- **WHEN** scheduler, preemption guard, IRQ-return scheduling, or context switch code changes
- **THEN** source-level checks or review notes MUST cover context-switch frame assumptions, interrupt frame assumptions, preemption-disable behavior, pending reschedule handling, and allocator exclusion in IRQ paths
- **AND** checks MUST record whether any clang/clangd diagnostics are current-change issues, historical diagnostics, or freestanding false positives

#### Scenario: Runtime smoke preserves cooperative behavior

- **WHEN** scheduler boundary cleanup changes runtime control flow
- **THEN** validation MUST confirm that cooperative yield, blocking/sleeping skip behavior, idle ownership, and timer-driven preemption behavior remain deterministic under the single-core scheduler model
- **AND** validation MUST NOT claim SMP balancing, per-CPU state, IPI, TLB shootdown, POSIX scheduling policy, or real-time scheduling semantics
