## ADDED Requirements

### Requirement: Scheduler concurrency stress preserves runnable ownership
BigOS SHALL validate that multi-core scheduler stress preserves the invariant that each runnable thread belongs to at most one CPU run queue while cross-CPU wakeups, timeout wakeups, and IRQ-return scheduling are active.

#### Scenario: remote wakeup cannot duplicate runnable state
- **WHEN** repeated wait-queue wakeups or timeout expirations make threads runnable on remote CPU domains under stress
- **THEN** each thread MUST become runnable on exactly one target CPU run queue
- **AND** stale wait-queue membership, stale sleep-list membership, or duplicate enqueue MUST be detected and handled through a deterministic diagnostic path

#### Scenario: AP IRQ-return scheduling uses local domain under stress
- **WHEN** an application processor returns from timer or scheduler-nudge interrupts during scheduler stress
- **THEN** it MUST select work only through its initialized scheduler domain
- **AND** it MUST NOT read or mutate bootstrap-only scheduler state

### Requirement: Scheduler lock ordering remains compatible with IPI and shootdown waits
BigOS SHALL keep per-CPU scheduler domain locks out of any path that waits for TLB shootdown completion or remote IPI acknowledgement.

#### Scenario: remote enqueue releases locks before remote wait
- **WHEN** scheduler code publishes runnable work to a remote CPU and requests a scheduler nudge
- **THEN** it MUST release the relevant run queue lock before any bounded wait that depends on the remote CPU
- **AND** the scheduler-nudge handler MUST NOT need a lock held by the requesting CPU to observe the reschedule request

#### Scenario: scheduler diagnostics identify lock-order violations
- **WHEN** scheduler stress detects duplicate ownership, invalid CPU placement, or lock-order violation
- **THEN** BigOS MUST emit deterministic diagnostics identifying the current CPU, target CPU, thread state, and scheduler operation
- **AND** it MUST fail closed instead of allowing the corrupted runnable state to continue

### Requirement: Scheduler hardening validation is reproducible
BigOS SHALL provide reproducible scheduler hardening validation for cross-CPU wakeups and AP scheduling when local multi-core runtime tooling supports it.

#### Scenario: bounded multi-core scheduler stress passes
- **WHEN** QEMU, xmake, cross-binutils, APIC support, and helper scripts are available
- **THEN** validation MUST include a bounded multi-core scheduler stress that observes work executing on more than one online CPU
- **AND** it MUST also confirm that the default bounded userland baseline still reaches its expected boot behavior

#### Scenario: scheduler stress cannot run locally
- **WHEN** runtime tooling needed for multi-core scheduler stress is unavailable
- **THEN** validation notes MUST record skipped scheduler runtime coverage and substitute source/build checks
- **AND** they MUST NOT claim emulator-validated cross-CPU scheduler hardening
