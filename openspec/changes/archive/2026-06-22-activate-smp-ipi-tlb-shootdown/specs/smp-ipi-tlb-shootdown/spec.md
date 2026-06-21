## ADDED Requirements

### Requirement: SMP IPI delivery is typed and bounded
BigOS SHALL provide a bounded x86_64 SMP IPI delivery boundary for internal kernel messages. The boundary MUST identify the IPI type, target online CPU or CPU set, delivery state, acknowledgement requirements, timeout behavior, and failure diagnostics without exposing a user-visible ABI.

#### Scenario: scheduler nudge uses typed IPI delivery
- **WHEN** the scheduler requests a remote CPU to observe runnable work
- **THEN** BigOS MUST deliver a scheduler-nudge IPI through the typed IPI boundary
- **AND** the delivery MUST preserve existing per-CPU run queue ownership and MUST NOT be treated as TLB shootdown completion

#### Scenario: TLB shootdown uses typed IPI delivery
- **WHEN** a page-table change requires remote CPU invalidation
- **THEN** BigOS MUST deliver a TLB-shootdown IPI to the bounded target CPU set
- **AND** each target CPU MUST either acknowledge completion or be excluded by a documented non-residency/offline rule before the requester reports completion

#### Scenario: unsupported target fails deterministically
- **WHEN** an IPI request targets an offline, failed, undiscovered, or otherwise non-schedulable CPU
- **THEN** BigOS MUST reject or exclude that target deterministically
- **AND** it MUST NOT wait forever for an acknowledgement from a CPU that cannot legally receive the IPI

### Requirement: IPI handlers remain IRQ-safe
BigOS SHALL run IPI handlers inside hard IRQ context using only IRQ-safe, non-blocking operations. IPI handlers MUST NOT allocate ordinary dynamic memory, perform filesystem or block I/O, wait on scheduler-managed blocking primitives, or enter user-copy paths.

#### Scenario: TLB-shootdown handler only invalidates and acknowledges
- **WHEN** a CPU receives a TLB-shootdown IPI
- **THEN** the handler MUST perform only the requested local TLB invalidation, update IRQ-safe acknowledgement state, and return through the LAPIC EOI boundary
- **AND** it MUST NOT mutate VMA collections, fault in pages, allocate frames, or block

#### Scenario: scheduler-nudge handler only records reschedule state
- **WHEN** a CPU receives a scheduler-nudge IPI
- **THEN** the handler MUST set or observe CPU-local reschedule state through the scheduler-owned IRQ-safe boundary
- **AND** it MUST NOT claim any TLB invalidation completion

### Requirement: Cross-CPU TLB shootdown completion is ordered
BigOS SHALL complete cross-CPU TLB shootdown only after page-table updates are visible to target CPUs and all required target CPUs have performed the requested local invalidation. The caller MUST NOT release frames, reuse address ranges, or return to user mode with stale remote translations still legal.

#### Scenario: mapping removal waits for remote invalidation
- **WHEN** BigOS clears a present user PTE for an address space that may be active on another online CPU
- **THEN** it MUST publish the PTE update before sending shootdown IPIs
- **AND** it MUST wait for required remote acknowledgements before releasing the frame or reporting the unmap/protection operation complete

#### Scenario: single-core fallback remains local
- **WHEN** BigOS runs with only the bootstrap CPU online or with SMP shootdown disabled
- **THEN** the TLB invalidation boundary MUST complete with local invalidation semantics
- **AND** it MUST preserve the current bounded userland baseline without requiring AP startup or IPI delivery

#### Scenario: shootdown timeout fails closed
- **WHEN** a target CPU does not acknowledge a required TLB shootdown before the bounded timeout
- **THEN** BigOS MUST enter a deterministic diagnostic or panic path
- **AND** it MUST NOT continue with frame reclamation or user-mode return that depends on the missing invalidation

### Requirement: Target CPU selection follows address-space residency
BigOS SHALL identify TLB shootdown targets from online CPU state and address-space residency recorded by an independent `mm context`. A CPU MUST be targeted when it may continue executing with the affected `mm context` address-space root and stale translations after the page-table update.

#### Scenario: active remote address space is targeted
- **WHEN** a remote online CPU is running or may return to the affected `mm context` without a CR3 switch that naturally discards stale translations
- **THEN** BigOS MUST include that CPU in the shootdown target set
- **AND** completion MUST require that CPU's local invalidation acknowledgement

#### Scenario: non-resident CPU is excluded
- **WHEN** an online CPU cannot legally hold translations for the affected address-space root
- **THEN** BigOS MAY exclude that CPU from the target set
- **AND** the exclusion MUST be based on explicit CPU/`mm context` residency state rather than a hidden BSP-only assumption

### Requirement: MM context owns address-space residency
BigOS SHALL introduce an independent `mm context` ownership object for user address spaces used by SMP shootdown. The `mm context` MUST track the page-table root, reference count, active CPU residency, teardown state, and a shootdown generation or equivalent ordering token.

#### Scenario: process owns an mm context
- **WHEN** a user process image is committed by exec, duplicated by fork, or released by exit/teardown
- **THEN** BigOS MUST update the owning `mm context` reference count and address-space root ownership consistently
- **AND** it MUST NOT free the page-table root while any process, CPU residency, or pending shootdown still references the `mm context`

#### Scenario: CPU residency updates on address-space switch
- **WHEN** a CPU switches into or out of a user address space
- **THEN** BigOS MUST update the active CPU residency state for the corresponding `mm context`
- **AND** shootdown target selection MUST use that residency state rather than only scanning current process pointers

#### Scenario: teardown waits for references
- **WHEN** address-space teardown marks an `mm context` as dying
- **THEN** BigOS MUST prevent new CPU residency for that context and wait for existing CPU residency or pending shootdown references to drain before reclaiming address-space-owned resources
- **AND** failure to drain within the bounded diagnostic policy MUST fail closed

### Requirement: SMP IPI and shootdown validation is reproducible
BigOS SHALL validate typed IPI delivery, IRQ-safe handler behavior, cross-CPU TLB shootdown completion, timeout/failure boundaries, and single-core fallback with deterministic source checks and bounded emulator smoke when local tooling supports it.

#### Scenario: source checks cover IPI invariants
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks for IPI vector classification, IRQ-safe handler restrictions, acknowledgement ordering, target CPU filtering, and absence of blocking operations in IPI handlers

#### Scenario: dedicated QEMU multi-core shootdown smoke
- **WHEN** QEMU, xmake, the x86_64 cross toolchain, helper scripts, and the dedicated TLB shootdown smoke build switch are available
- **THEN** validation MUST include a bounded multi-core smoke that observes IPI delivery, `mm context` residency tracking, and remote TLB shootdown completion
- **AND** the smoke MUST also confirm the bounded userland baseline still reaches its expected boot behavior

#### Scenario: missing runtime tooling is explicit
- **WHEN** QEMU, Bochs, cross-binutils, APIC support, display/ROM configuration, or helper script support is unavailable
- **THEN** validation notes MUST record skipped runtime checks, substitute checks, and residual SMP IPI or shootdown risk
- **AND** they MUST NOT claim emulator-validated cross-CPU TLB shootdown
