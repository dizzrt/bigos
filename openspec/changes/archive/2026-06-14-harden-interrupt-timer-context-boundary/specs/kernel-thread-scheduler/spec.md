## ADDED Requirements

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
