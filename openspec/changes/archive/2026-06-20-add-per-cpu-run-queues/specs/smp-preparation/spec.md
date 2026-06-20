## MODIFIED Requirements

### Requirement: Scheduler SMP Gate
The scheduler SHALL use explicit CPU-owned run queue, wait queue, sleep list, current thread, idle thread, and reschedule flag boundaries before multiple CPUs can schedule concurrently. Cross-CPU scheduling and remote wakeup are enabled only through the bounded per-CPU run queue contract introduced by this change.

#### Scenario: Run queue ownership is explicit
- **WHEN** scheduler state such as ready queues, wait queues, sleeping lists, current thread, idle thread, or reschedule flags is accessed
- **THEN** the access MUST identify the owning CPU and locking boundary
- **AND** code MUST NOT rely on a hidden BSP-only singleton when executing on an application processor

#### Scenario: Cross-CPU scheduling uses explicit boundary
- **WHEN** BigOS migrates runnable work, wakes a remote CPU, balances initial placement, or requests another CPU to reschedule
- **THEN** the operation MUST use the documented per-CPU run queue and remote reschedule boundary
- **AND** it MUST NOT be implemented as an implicit side effect of AP startup, timer calibration, TLB invalidation, or generic interrupt dispatch

#### Scenario: Unsupported SMP behavior remains gated
- **WHEN** code references CPU hotplug, NUMA, complete load balancing, generic IPI routing, cross-CPU TLB shootdown, or full APIC default interrupt delivery
- **THEN** those references MUST remain future requirements unless a dedicated change explicitly enables them
- **AND** per-CPU run queue validation MUST NOT be described as validation for those deferred capabilities

### Requirement: Interrupt Routing Assumptions
The kernel SHALL document and enforce interrupt-routing assumptions for the current single-core baseline, the AP startup/per-CPU timer baseline, the per-CPU run queue scheduler baseline, and later full SMP transition.

#### Scenario: Legacy interrupt path remains stable
- **WHEN** the kernel uses the current i8259, PIT, keyboard, syscall, and exception paths outside the AP startup/per-CPU timer and scheduler SMP baselines
- **THEN** those paths MUST continue to route through the existing bootstrap CPU-compatible dispatch model

#### Scenario: APIC and per-CPU timer are controlled activation scope
- **WHEN** BigOS enables AP startup or per-CPU local timer support
- **THEN** LAPIC, IOAPIC initialization boundaries, AP startup IPIs, LAPIC EOI, PIT-referenced LAPIC timer calibration, APIC-backed scheduler timer interrupt ownership, and local APIC timer delivery MUST be treated as part of the controlled x86_64 activation scope
- **AND** this activation MUST NOT by itself imply complete APIC-backed external IRQ delivery for all devices, cross-CPU TLB shootdown, CPU hotplug, NUMA, or runtime parity for non-default backends

#### Scenario: Scheduler nudge is controlled activation scope
- **WHEN** BigOS enables per-CPU run queues and cross-CPU wakeups
- **THEN** the scheduler nudge interrupt or equivalent LAPIC IPI delivery MUST be treated as a scheduler-owned activation scope
- **AND** it MUST NOT be described as complete generic IPI support, complete TLB shootdown, or complete APIC default interrupt delivery

#### Scenario: IPI shootdown and full interrupt migration remain future dependencies
- **WHEN** SMP preparation references remote TLB shootdown, shootdown acknowledgement, CPU hotplug, NUMA, or full APIC default interrupt delivery
- **THEN** those references MUST remain future requirements unless a dedicated later change explicitly enables them
- **AND** AP startup/per-CPU timer or per-CPU run queue validation MUST NOT be described as complete SMP memory-management or interrupt-delivery validation
