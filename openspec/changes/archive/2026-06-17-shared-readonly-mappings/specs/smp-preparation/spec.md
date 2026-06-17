## ADDED Requirements

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
