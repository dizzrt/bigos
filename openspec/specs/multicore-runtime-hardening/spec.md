# multicore-runtime-hardening Specification

## Purpose
TBD - created by archiving change harden-multicore-runtime. Update Purpose after archive.
## Requirements
### Requirement: Multi-core runtime hardening is validated under concurrency stress
BigOS SHALL provide a bounded multi-core hardening validation path that exercises scheduler concurrency, cross-CPU wakeups, IPI delivery, TLB shootdown completion, and the default bounded userland baseline without changing the default boot behavior.

#### Scenario: stress covers scheduler, IPI, and shootdown interaction
- **WHEN** the multi-core hardening validation path is enabled on an emulator configuration with more than one online CPU
- **THEN** BigOS MUST create deterministic stress over remote runnable enqueue, scheduler nudge delivery, typed IPI dispatch, and TLB shootdown acknowledgement
- **AND** it MUST report pass/fail through bounded serial diagnostics rather than relying on interactive observation

#### Scenario: default boot remains unchanged
- **WHEN** the multi-core hardening validation path is disabled
- **THEN** BigOS MUST preserve the normal bounded userland boot path
- **AND** it MUST NOT require the stress workload, extra user ABI, or additional device backend to reach the default shell/init baseline

### Requirement: Shared-state lock ordering is explicit across multi-core subsystems
BigOS SHALL define and enforce lock ordering for scheduler domains, CPU topology state, IPI request state, TLB shootdown completion state, and `mm context` residency so multi-core validation cannot deadlock under cross-CPU wakeup or address-space invalidation.

#### Scenario: wait path does not hold scheduler locks
- **WHEN** a CPU waits for remote IPI or TLB shootdown acknowledgement
- **THEN** it MUST NOT hold a per-CPU run queue lock or any lock required by the target CPU's IRQ handler
- **AND** it MUST fail closed on bounded timeout rather than spinning forever

#### Scenario: IRQ handlers use only IRQ-safe state
- **WHEN** a timer, scheduler-nudge, or TLB-shootdown interrupt handler runs
- **THEN** it MUST update only CPU-local or explicitly IRQ-safe acknowledgement state
- **AND** it MUST NOT allocate ordinary dynamic memory, perform filesystem/block I/O, or wait on scheduler-managed blocking primitives

### Requirement: Multi-core failures are deterministic and fail closed
BigOS SHALL produce deterministic diagnostics and fail closed for multi-core timeout or fault conditions that could otherwise expose stale translations, duplicate runnable state, or failed CPUs to normal execution.

#### Scenario: illegal target is rejected deterministically
- **WHEN** scheduler, IPI, or TLB shootdown code targets an offline, failed, undiscovered, or non-schedulable CPU
- **THEN** BigOS MUST reject the target or exclude it through an explicit documented rule
- **AND** it MUST emit diagnostics sufficient to identify the target CPU state and failing subsystem

#### Scenario: required acknowledgement times out
- **WHEN** a required scheduler nudge, IPI delivery, or TLB shootdown acknowledgement does not complete within the bounded timeout
- **THEN** BigOS MUST enter a deterministic diagnostic or panic path
- **AND** it MUST NOT continue with execution that depends on the missing acknowledgement

### Requirement: Multi-core validation records unavailable runtime coverage
BigOS SHALL record unavailable emulator, toolchain, APIC, or helper-script coverage explicitly when multi-core runtime hardening cannot be fully validated locally.

#### Scenario: runtime tooling is missing
- **WHEN** QEMU, Bochs, xmake, cross-binutils, APIC support, display/ROM configuration, or helper-script support is unavailable
- **THEN** validation notes MUST list the skipped runtime checks, substitute source/build checks, and residual multi-core risk
- **AND** they MUST NOT claim emulator-validated multi-core scheduling, IPI delivery, or TLB shootdown completion

