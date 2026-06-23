## ADDED Requirements

### Requirement: AP startup failures remain isolated from multi-core consumers
BigOS SHALL keep failed or timed-out application processors isolated from scheduler, timer, IPI, and TLB shootdown consumers.

#### Scenario: AP startup timeout marks CPU failed
- **WHEN** an application processor does not acknowledge online state within the bounded startup timeout
- **THEN** BigOS MUST keep that CPU out of the online scheduler domain set, IPI target set, and TLB shootdown required target set
- **AND** it MUST emit deterministic diagnostics identifying the CPU slot, APIC id, startup phase, and timeout reason

#### Scenario: failed AP cannot receive ordinary work
- **WHEN** scheduler placement or IPI target selection observes a CPU marked failed by AP startup
- **THEN** BigOS MUST reject or skip that CPU through an explicit rule
- **AND** it MUST NOT enqueue ordinary runnable work or wait for an acknowledgement from that CPU

### Requirement: Per-CPU timer hardening preserves CPU-local accounting
BigOS SHALL validate that per-CPU timer interrupts on online CPUs update only initialized CPU-local timer and scheduler state.

#### Scenario: AP timer tick before initialization is rejected
- **WHEN** an AP timer interrupt is observed before that CPU's local timer state and scheduler domain are initialized
- **THEN** BigOS MUST reject the tick or route it to a deterministic diagnostic path
- **AND** it MUST NOT mutate bootstrap-only timer or scheduler state as a fallback

#### Scenario: AP timer stress uses local state
- **WHEN** bounded multi-core timer stress runs with more than one online CPU
- **THEN** each valid AP timer tick MUST update the receiving CPU's timer accounting and scheduler preemption state through CPU-local boundaries
- **AND** it MUST NOT require filesystem, block I/O, user-copy, or ordinary dynamic allocation from IRQ context

### Requirement: APIC fallback diagnostics are explicit
BigOS SHALL provide deterministic diagnostics when LAPIC, IOAPIC, or per-CPU timer setup cannot support the multi-core hardening path.

#### Scenario: APIC-backed path unavailable
- **WHEN** LAPIC startup, IOAPIC routing, or per-CPU local timer setup is unavailable or invalid
- **THEN** BigOS MUST either fail closed or continue only through a documented BSP-only diagnostic fallback
- **AND** validation notes MUST NOT claim multi-core APIC-backed scheduling, IPI, or timer hardening for that fallback

#### Scenario: fallback does not change layout assumptions
- **WHEN** BigOS uses a BSP-only diagnostic fallback after APIC setup failure
- **THEN** it MUST preserve existing boot addresses, AP trampoline reservation assumptions, kernel page-table root assumptions, interrupt vector ownership, and user ABI
- **AND** any required layout change MUST be handled by a separate reviewed change
