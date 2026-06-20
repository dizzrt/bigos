## Purpose

Define the bounded per-CPU run queue scheduler baseline for BigOS, including CPU-owned scheduler domains, explicit runnable placement, controlled cross-CPU wakeup, scheduler-owned remote reschedule nudges, multi-core timer-driven scheduling, and validation boundaries without claiming CPU hotplug, complete load balancing, generic IPI routing, TLB shootdown, or complete APIC default interrupt delivery.

## Requirements

### Requirement: Per-CPU run queue ownership
BigOS SHALL provide a bounded scheduler domain for each online CPU. Each domain MUST own its local runnable queue, current thread, idle thread, sleep list, reschedule state, and scheduler-visible accounting without relying on a hidden BSP-only singleton.

#### Scenario: Online CPU has scheduler domain
- **WHEN** a CPU is marked online and is allowed to participate in scheduling
- **THEN** BigOS MUST initialize a scheduler domain for that CPU before it can run ordinary kernel threads
- **AND** the domain MUST identify the owning CPU id and its current, idle, runnable, sleeping, and reschedule state

#### Scenario: Offline CPU cannot receive work
- **WHEN** scheduler placement considers a CPU whose startup state is offline, failed, empty, or otherwise not schedulable
- **THEN** BigOS MUST NOT enqueue ordinary runnable work on that CPU
- **AND** it MUST choose another online CPU or fail the placement through a deterministic diagnostic path

#### Scenario: BSP-only fallback remains valid
- **WHEN** BigOS boots with only the bootstrap CPU online or with SMP scheduler support disabled
- **THEN** the scheduler MUST degrade to a single CPU domain that preserves the existing bounded scheduler behavior
- **AND** it MUST NOT require AP startup, IPI delivery, or APIC timer delivery to run the bounded userland baseline

### Requirement: Cross-CPU runnable placement
BigOS SHALL place newly runnable kernel threads onto a selected online CPU run queue through a bounded policy that is explicit about CPU ownership and locking.

#### Scenario: New thread receives a target CPU
- **WHEN** BigOS creates a kernel thread after scheduler initialization
- **THEN** it MUST assign the thread to an online scheduler domain before making it runnable
- **AND** the placement MUST NOT allocate memory, block, or inspect hosted runtime state while holding run queue locks

#### Scenario: Wakeup can enqueue remotely
- **WHEN** a wait queue wakeup or timeout makes a blocked or sleeping thread runnable and the selected target CPU differs from the caller CPU
- **THEN** BigOS MUST enqueue the thread on the target CPU run queue under the target scheduler domain protection
- **AND** it MUST publish the runnable state before requesting a remote reschedule

#### Scenario: Thread is runnable on exactly one queue
- **WHEN** a thread transitions into the runnable state
- **THEN** it MUST be linked into at most one CPU run queue
- **AND** duplicate enqueue, stale wait queue membership, or stale sleep list membership MUST be rejected or repaired before the thread becomes eligible to run

### Requirement: Remote reschedule request
BigOS SHALL provide a remote reschedule request boundary that wakes or nudges a target CPU after work is enqueued remotely, without implying complete generic IPI or TLB shootdown support.

#### Scenario: Remote enqueue requests target CPU scheduling
- **WHEN** BigOS enqueues runnable work on a CPU other than the caller CPU
- **THEN** it MUST set a reschedule-pending indication visible to the target CPU
- **AND** it MUST notify the target CPU through the scheduler nudge boundary when the target may be idle or running a preemptible ordinary thread

#### Scenario: Scheduler nudge is not TLB shootdown
- **WHEN** the scheduler nudge uses LAPIC IPI delivery or an equivalent interrupt mechanism
- **THEN** the mechanism MUST be scoped to reschedule observation
- **AND** it MUST NOT be documented or validated as cross-CPU TLB shootdown, CPU hotplug, generic IPI routing, or complete APIC default interrupt delivery

#### Scenario: Local enqueue avoids unnecessary remote notification
- **WHEN** BigOS enqueues runnable work on the caller CPU
- **THEN** it MAY satisfy the reschedule request through the local pending-reschedule boundary
- **AND** it MUST NOT require a remote IPI to make local work visible

### Requirement: Multi-core timer-driven scheduling
BigOS SHALL allow each online CPU with a ready local timer to account its current ordinary thread and process safe IRQ-return preemption using that CPU's scheduler domain.

#### Scenario: AP timer tick uses AP scheduler domain
- **WHEN** an application processor receives a valid local timer tick while scheduler SMP is enabled
- **THEN** BigOS MUST account the current thread and reschedule state through that AP's scheduler domain
- **AND** it MUST NOT read or mutate BSP-only run queue state for that tick

#### Scenario: IRQ-return preemption is CPU-local
- **WHEN** a CPU returns from a timer or scheduler-nudge interrupt with reschedule pending and preemption allowed
- **THEN** BigOS MUST select the next runnable thread from that CPU's scheduler domain or continue idle if no local runnable thread exists
- **AND** the switch MUST preserve the existing interrupt frame ABI and context-switch ABI

#### Scenario: Idle CPU runs local work
- **WHEN** a CPU is idle and its run queue becomes non-empty
- **THEN** the CPU MUST be able to leave idle at a scheduler-owned boundary and run local runnable work
- **AND** the idle path MUST preserve interrupt-readiness assumptions required for timer or nudge delivery

### Requirement: Scheduler SMP validation
BigOS SHALL validate per-CPU run queues and cross-CPU wakeups with deterministic source checks and bounded multi-core emulator smoke when local tooling supports it.

#### Scenario: Source checks cover SMP scheduler invariants
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks for CPU-local scheduler state access, remote enqueue publication order, lock ordering, IRQ-context allocation exclusions, and absence of BSP-only scheduler access from AP tick and IRQ-return paths

#### Scenario: QEMU multi-core scheduler smoke
- **WHEN** QEMU, xmake, the x86_64 cross toolchain, and helper scripts are available
- **THEN** validation MUST include a bounded multi-core scheduler smoke that observes runnable work executing on more than one online CPU
- **AND** the smoke MUST confirm the bounded userland baseline still reaches its expected boot behavior

#### Scenario: Missing runtime tooling is explicit
- **WHEN** QEMU, Bochs, cross-binutils, APIC support, display/ROM configuration, or helper script support is unavailable
- **THEN** validation notes MUST record skipped runtime checks, substitute checks, and residual multi-core scheduler risk
- **AND** they MUST NOT claim emulator-validated cross-CPU scheduling
