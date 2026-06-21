## ADDED Requirements

### Requirement: IPI shootdown activation fulfills the prepared TLB boundary
BigOS SHALL treat this change as the dedicated activation of the SMP-preparation IPI shootdown and cross-CPU TLB invalidation boundary. Earlier preparation requirements that left IPI shootdown as future work MUST now resolve to the bounded `smp-ipi-tlb-shootdown` capability while still keeping CPU hotplug, NUMA, and complete APIC default interrupt delivery out of scope.

#### Scenario: cross-CPU invalidation no longer degrades silently
- **WHEN** real SMP execution is enabled and a page-table change affects an address space that may be active on a remote online CPU
- **THEN** BigOS MUST use the cross-CPU shootdown boundary rather than local-only invalidation
- **AND** it MUST preserve the SMP-preparation ordering rule that page-table updates become visible before invalidation completion is reported

#### Scenario: future dependencies remain bounded
- **WHEN** implementation or validation references CPU hotplug, NUMA, complete load balancing, broad device IRQ migration, or complete APIC-backed default interrupt delivery
- **THEN** those references MUST remain future or non-goal statements
- **AND** they MUST NOT be claimed as delivered by IPI shootdown activation

### Requirement: IRQ-safe locking contract is active for SMP interrupt paths
BigOS SHALL enforce the SMP-preparation locking classification for active IPI, shootdown, scheduler nudge, timer IRQ, and IRQ-return preemption paths. State shared between hard IRQ handlers and ordinary kernel context MUST be protected by IRQ-safe locks, interrupt disable boundaries, atomic operations, or documented architecture fences.

#### Scenario: hard IRQ path avoids blocking primitives
- **WHEN** code runs from a timer IRQ, scheduler-nudge IPI, TLB-shootdown IPI, or IRQ-return preemption boundary
- **THEN** it MUST NOT wait on scheduler-managed blocking primitives, perform filesystem/block I/O, or allocate through a path that can block
- **AND** it MUST use only synchronization documented as valid in that context

#### Scenario: publication to remote CPUs is ordered
- **WHEN** ordinary kernel context publishes IPI request state, shootdown target masks, page-table updates, or reschedule state for observation by remote CPUs
- **THEN** the selected lock, interrupt boundary, atomic operation, or architecture fence MUST provide the required visibility before the remote CPU can acknowledge or act on that state
