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

BigOS SHALL provide a minimal single-core round-robin scheduler that selects runnable kernel threads from a bounded run queue and rotates the current thread through explicit cooperative yield.

#### Scenario: Yield switches to another runnable thread

- **WHEN** a running kernel thread calls `yield()` and at least one other runnable non-idle thread exists
- **THEN** the scheduler MUST place the current thread back on the runnable queue
- **AND** the scheduler MUST switch to the next runnable thread in round-robin order

#### Scenario: Yield returns when no peer is runnable

- **WHEN** a running kernel thread calls `yield()` and no other non-idle runnable thread exists
- **THEN** the scheduler MUST continue running the current thread or switch to idle without corrupting the run queue

#### Scenario: Scheduler remains single-core

- **WHEN** scheduler APIs or documentation describe scheduling behavior
- **THEN** they MUST state that the scheduler is single-core only and does not provide SMP balancing, IPI, per-CPU run queues, or cross-CPU synchronization

#### Scenario: Stage 4 does not preempt on IRQ return

- **WHEN** timer IRQ0 fires during stage 4 scheduler runtime
- **THEN** BigOS MUST NOT perform IRQ-return preemptive context switching in this change
- **AND** any timer reschedule intent MUST be recorded only as bounded state for cooperative or future scheduling paths

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
