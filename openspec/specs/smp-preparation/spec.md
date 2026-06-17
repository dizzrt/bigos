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
