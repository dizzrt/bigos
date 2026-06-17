## ADDED Requirements

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

#### Scenario: blocking primitives and timer ownership capability remains non-preemptive
- **WHEN** a thread becomes runnable because of wakeup or timeout
- **THEN** BigOS MUST NOT perform timer-driven IRQ-return context switching in this stage
- **AND** the existing interrupt frame ABI, context-switch frame layout, EOI ordering, and idle-thread ownership MUST remain unchanged

### Requirement: Scheduler exposes blocking context rules
BigOS SHALL provide scheduler-facing helpers or documented rules that allow kernel code to determine whether the current context may block.

#### Scenario: Blocking is allowed only from ordinary thread context
- **WHEN** a blocking wait or sleep API checks the current context
- **THEN** it MUST succeed only for ordinary non-interrupt thread context with scheduler state initialized and no active scheduler critical section
- **AND** it MUST reject IRQ handlers, CPU exception handlers, fatal diagnostics, and contexts where interrupts are disabled in a way that would prevent timer progress

#### Scenario: Scheduler critical section prevents sleep
- **WHEN** kernel code owns scheduler run-queue or wait-queue critical state
- **THEN** blocking wait APIs MUST NOT schedule away the current thread from inside that critical section
- **AND** source-level checks or validation notes MUST cover this restriction
