## MODIFIED Requirements

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

## ADDED Requirements

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
BigOS SHALL provide a minimal bounded priority hook, static priority field, or reserved scheduler policy slot without requiring a complete priority scheduler in the documented capability.

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
