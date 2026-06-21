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
- **WHEN** the kernel uses the current i8259, PIT, keyboard, syscall, and exception paths outside the AP startup/per-CPU timer baseline
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

### Requirement: 共享只读映射使用既有 TLB invalidation boundary
BigOS SHALL express all TLB-visible changes for shared read-only mappings through the existing SMP-preparation TLB invalidation boundary. The single-core baseline MUST complete these operations with local invalidation, while the request shape MUST retain enough information for future cross-CPU shootdown: affected address-space root, address range or page, invalidation reason, and required completion ordering.

#### Scenario: active root unmap 执行本地 invalidation
- **WHEN** BigOS clears a present shared read-only file-backed PTE from the active address-space root during unmap, protection change, or teardown of the current process
- **THEN** it MUST issue invalidation through the TLB boundary before returning to user mode
- **AND** with real SMP disabled the boundary MUST resolve to current-CPU local invalidation only

#### Scenario: inactive root 记录无需立即 invlpg 的条件
- **WHEN** BigOS clears shared read-only PTEs from an inactive address-space root that will not be re-entered before CR3 switch or teardown completion
- **THEN** it MAY avoid immediate local `invlpg` only if the caller documents the inactive-root condition through the invalidation boundary or equivalent teardown rule
- **AND** it MUST NOT rely on hidden global single-core assumptions that would block future shootdown integration

### Requirement: 共享只读元数据标注未来锁边界
BigOS SHALL classify shared read-only mapping metadata as SMP-sensitive state even while the runtime remains single-core. Access to the shared page table, frame references, and backing-object retained references MUST be reachable only from contexts whose future locking and memory-ordering requirements are explicit and compatible with the existing SMP preparation contract.

#### Scenario: page fault 可在可阻塞进程上下文更新共享表
- **WHEN** a file-backed read fault occurs in ordinary process context where allocation and page/buffer cache reads are permitted
- **THEN** BigOS MAY update the shared read-only page table, retain backing references, and publish PTEs after the required ordering is satisfied
- **AND** the implementation MUST identify the protection boundary that will become the future SMP lock or equivalent synchronization primitive

#### Scenario: IRQ 或调度临界区不装入共享页
- **WHEN** shared read-only page lookup or materialization would require allocation, backing-file retention, page/buffer cache load, or shared table mutation from IRQ context, scheduler critical section, preemption-disabled region, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter the documented diagnostic path
- **AND** it MUST NOT issue blocking I/O, mutate shared mapping metadata, or publish a successful PTE from that context

### Requirement: 共享只读发布顺序满足未来远端 CPU 可见性
BigOS SHALL order shared read-only page publication so frame contents, shared metadata, frame references, and page-table entries become visible in a sequence compatible with future remote CPU observation. Page-table updates MUST become visible before the invalidation boundary reports completion, and shared metadata removal MUST not make a frame reclaimable before all PTE references have been cleared.

#### Scenario: publish 顺序先内容后 PTE
- **WHEN** BigOS materializes a new shared read-only file page
- **THEN** it MUST finish loading and zero-filling frame contents, record shared metadata, and retain the frame before publishing any present user PTE
- **AND** the publication ordering MUST be expressed through the selected lock, interrupt boundary, atomic operation, or architecture fence required by the SMP preparation contract

#### Scenario: removal 顺序先清 PTE 后释放 frame
- **WHEN** BigOS removes the last mapping of a shared read-only file page
- **THEN** it MUST clear relevant PTEs and complete required invalidation before making the frame reclaimable
- **AND** it MUST NOT free the frame while a still-present PTE in any address space can legally reference it

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
