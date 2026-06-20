## MODIFIED Requirements

### Requirement: Interrupt Routing Assumptions
The kernel SHALL document and enforce interrupt-routing assumptions for the current single-core baseline, the AP startup/per-CPU timer baseline, and later full SMP transition.

#### Scenario: Legacy interrupt path remains stable
- **WHEN** the kernel uses the current i8259, PIT, keyboard, syscall, and exception paths outside the AP startup/per-CPU timer baseline
- **THEN** those paths MUST continue to route through the existing bootstrap CPU-compatible dispatch model

#### Scenario: APIC and per-CPU timer are controlled activation scope
- **WHEN** BigOS enables AP startup or per-CPU local timer support
- **THEN** LAPIC, IOAPIC initialization boundaries, AP startup IPIs, LAPIC EOI, PIT-referenced LAPIC timer calibration, APIC-backed scheduler timer interrupt ownership, and local APIC timer delivery MUST be treated as part of the controlled x86_64 activation scope
- **AND** this activation MUST NOT by itself imply complete APIC-backed external IRQ delivery for all devices, cross-CPU scheduling, IPI TLB shootdown, or runtime parity for non-default backends

#### Scenario: IPI shootdown and cross-CPU scheduling remain future dependencies
- **WHEN** SMP preparation references remote TLB shootdown, cross-CPU wakeups, runnable work migration, load balancing, or full APIC default interrupt delivery
- **THEN** those references MUST remain future requirements unless a dedicated later change explicitly enables them
- **AND** AP startup/per-CPU timer validation MUST NOT be described as complete SMP scheduling validation

### Requirement: Validation Boundary
The SMP preparation work SHALL be validated according to the activated SMP-sensitive scope. Preparation-only work can use single-core validation, while AP startup/per-CPU timer activation MUST include bounded multi-core boot validation when local tooling supports it.

#### Scenario: Single-core validation is sufficient for preparation-only changes
- **WHEN** a change only introduces or refines SMP preparation artifacts without starting application processors or enabling per-CPU local timers
- **THEN** validation MUST cover compilation and the narrowest useful single-core boot or smoke path for affected subsystems without requiring AP startup

#### Scenario: AP startup validation covers bounded multi-core behavior
- **WHEN** BigOS enables AP startup or per-CPU local timer behavior
- **THEN** validation MUST include the narrowest useful multi-core emulator smoke when QEMU or Bochs support is available
- **AND** the validation MUST observe AP online acknowledgement and per-CPU timer progress without claiming cross-CPU scheduler throughput

#### Scenario: Missing emulator or toolchain is reported
- **WHEN** QEMU, Bochs, cross-binutils, or required local configuration is unavailable
- **THEN** validation notes MUST record the skipped runtime checks, substitute checks, and residual risk instead of claiming real SMP or emulator validation
