## Purpose

Define the single-core-compatible SMP preparation contract for BigOS: explicit CPU-local state boundaries, IRQ-safe locking classification, scheduler and interrupt routing gates, TLB invalidation preparation, memory-ordering rules, and validation expectations without enabling real multi-core execution.

## Requirements

### Requirement: Single-Core-Compatible SMP Boundary
The kernel SHALL introduce SMP preparation boundaries that preserve the current single-core runtime behavior until a later change explicitly enables real multi-core execution.

#### Scenario: Default boot remains single-core
- **WHEN** the kernel boots with the SMP preparation boundaries present
- **THEN** only the bootstrap CPU is allowed to run kernel scheduling, interrupt dispatch, syscall dispatch, and user execution paths

#### Scenario: Existing ABI remains stable
- **WHEN** SMP preparation code is integrated into the current kernel baseline
- **THEN** boot addresses, linker addresses, interrupt vectors, syscall ABI, page-table layout, disk layout, and user-visible process behavior remain unchanged

### Requirement: Locking Model Classification
The kernel SHALL classify synchronization primitives by execution context before they are used for SMP-sensitive state.

#### Scenario: IRQ context uses only IRQ-safe protection
- **WHEN** code runs from exception, IRQ, timer tick, or IRQ-return preemption paths
- **THEN** it MUST use only protection that is documented as IRQ-safe and non-blocking for that path

#### Scenario: Blocking paths stay outside hard IRQ context
- **WHEN** code can sleep, wait on a queue, allocate memory that can block, or enter scheduler-managed blocking state
- **THEN** it MUST NOT be callable from hard IRQ context under the SMP preparation contract

### Requirement: Per-CPU State Contract
The kernel SHALL define a per-CPU state contract for CPU-local execution state while allowing the current implementation to map all state to the bootstrap CPU.

#### Scenario: Current execution state has CPU ownership
- **WHEN** kernel code queries the current thread, current process, current address space, kernel stack/TSS ownership, IRQ nesting, preemption disable depth, or pending reschedule state
- **THEN** the query MUST be expressed through a CPU-local state boundary rather than a hidden process-wide or system-wide singleton assumption

#### Scenario: Bootstrap-only fallback is explicit
- **WHEN** real SMP execution is disabled
- **THEN** per-CPU state access MUST resolve to a documented bootstrap CPU slot and MUST fail closed or panic on unsupported non-bootstrap CPU access

### Requirement: Scheduler SMP Gate
The scheduler SHALL remain single-core by default while exposing the boundaries that must be protected before multiple CPUs can schedule concurrently.

#### Scenario: Run queue ownership is explicit
- **WHEN** scheduler state such as ready queues, wait queues, sleeping lists, current thread, idle thread, or reschedule flags is accessed
- **THEN** the access MUST identify the ownership and locking boundary needed before cross-CPU scheduling can be enabled

#### Scenario: No implicit cross-CPU scheduling
- **WHEN** SMP preparation is complete but real SMP execution is still disabled
- **THEN** the scheduler MUST NOT migrate runnable work to another CPU or assume another CPU can perform wakeups, load balancing, or idle-thread ownership

### Requirement: Interrupt Routing Assumptions
The kernel SHALL document and enforce interrupt-routing assumptions for the single-core baseline and future SMP transition.

#### Scenario: Legacy interrupt path remains stable
- **WHEN** the kernel uses the current i8259, PIT, keyboard, syscall, and exception paths
- **THEN** those paths MUST continue to route through the existing bootstrap CPU-compatible dispatch model

#### Scenario: APIC and IPI remain future dependencies
- **WHEN** SMP preparation references LAPIC, IOAPIC, per-CPU timers, or IPI delivery
- **THEN** those references MUST be treated as future requirements and MUST NOT make the default runtime depend on APIC-backed interrupt delivery

### Requirement: TLB Shootdown Preparation
The memory-management layer SHALL expose a TLB invalidation boundary that can degrade to local invalidation on the single-core baseline and can later host cross-CPU shootdown.

#### Scenario: Single-core invalidation remains local
- **WHEN** page-table permissions, mappings, address-space teardown, demand-zero materialization, or COW transitions require TLB invalidation while SMP is disabled
- **THEN** the invalidation boundary MUST complete using local invalidation semantics appropriate for the current CPU

#### Scenario: Future cross-CPU invalidation has explicit inputs
- **WHEN** a later SMP implementation extends the invalidation boundary
- **THEN** it MUST have enough information to identify the affected address space, address range or page, target CPU set, and required completion ordering

### Requirement: Memory Ordering Rules
The kernel SHALL define memory-ordering rules for SMP-prepared synchronization, scheduler state, interrupt-visible state, and page-table updates.

#### Scenario: Shared state publication is ordered
- **WHEN** kernel state becomes visible to IRQ handlers, scheduler paths, user-memory fault handlers, or future remote CPUs
- **THEN** the publication MUST specify the required ordering through the selected lock, interrupt disable boundary, atomic operation, or architecture fence

#### Scenario: Page-table updates are ordered before invalidation completion
- **WHEN** a mapping or permission change requires TLB invalidation
- **THEN** the page-table update MUST become visible before the invalidation boundary reports completion

### Requirement: Validation Boundary
The SMP preparation work SHALL be validated without requiring real multi-core execution.

#### Scenario: Single-core validation is sufficient for this change
- **WHEN** the SMP preparation artifacts are implemented
- **THEN** validation MUST cover compilation and the narrowest useful single-core boot or smoke path for affected subsystems without requiring AP startup

#### Scenario: Missing emulator or toolchain is reported
- **WHEN** QEMU, Bochs, cross-binutils, or required local configuration is unavailable
- **THEN** validation notes MUST record the skipped runtime checks, substitute checks, and residual risk instead of claiming real SMP or emulator validation
