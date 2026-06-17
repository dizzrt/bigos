## ADDED Requirements

### Requirement: 主动 unmap 释放用户地址空间资源

BigOS SHALL release user address-space resources removed by an explicit anonymous unmap operation using the same ownership and safety rules as process teardown. The release path MUST clear affected leaf PTEs, release owned frames or decrement COW/shared references, reclaim empty dynamic page-table pages when ownership metadata permits, and invalidate affected current-CPU translations through the existing invalidation boundary.

#### Scenario: unmap 释放 owned 匿名页

- **WHEN** explicit unmap removes a materialized anonymous page exclusively owned by the current process
- **THEN** BigOS MUST clear the leaf PTE, invalidate the affected current-CPU translation, and return the owned frame through the appropriate allocator path
- **AND** it MUST update VMA/materialization accounting before the range can be considered unmapped

#### Scenario: unmap 递减 COW 或共享只读引用

- **WHEN** explicit unmap removes a materialized page that is COW-shared or otherwise reference-counted between processes
- **THEN** BigOS MUST decrement the frame reference count and return the frame only when the count reaches zero
- **AND** another process that still maps the frame MUST continue to observe valid contents and MUST NOT see a premature free

#### Scenario: unmap 后回收空页表页

- **WHEN** clearing unmapped leaf PTEs leaves a dynamically owned PT, PD, or PDPT page empty
- **THEN** BigOS MUST reclaim that page-table page according to the existing ownership metadata and empty-page-table reclamation rules
- **AND** it MUST NOT clear or free boot, kernel higher-half, direct-map, KVMEM, recursive self-mapping, or other non-owned static page tables

### Requirement: protection change 遵守 TLB invalidation 与页表权限边界

BigOS SHALL apply protection changes to present user PTEs in a way that never grants page-table access wider than VMA policy and observes the existing TLB invalidation boundary. Permission reductions MUST invalidate affected current-CPU translations before returning success to user mode.

#### Scenario: present PTE 权限被收紧

- **WHEN** protection change removes write or execute access from a range containing present user PTEs
- **THEN** BigOS MUST update those PTEs to permissions no wider than the new VMA permissions
- **AND** it MUST invalidate affected current-CPU translations before the syscall returns success

#### Scenario: 未物化页记录新权限

- **WHEN** protection change covers registered-but-unmaterialized anonymous pages
- **THEN** BigOS MUST update VMA permissions so future demand paging materializes those pages with the new permissions
- **AND** it MUST NOT allocate or map pages solely to apply the protection change

### Requirement: lifecycle operations run only from safe process context

BigOS SHALL execute explicit anonymous unmap and protection-change operations only from safe process syscall context where allocation, page-table modification, process lookup, and TLB invalidation are permitted. The operations MUST NOT run from IRQ context, scheduler critical sections, reaper teardown of another active process, or while the target CR3/stack state makes resource release unsafe.

#### Scenario: ordinary syscall context may modify current address space

- **WHEN** the current running user process invokes anonymous unmap or protection change from ordinary syscall context
- **THEN** BigOS MAY modify that process's VMA collection and user low-half page tables after all validation and staging succeeds
- **AND** it MUST preserve the active kernel stack, syscall return path, and process table iteration safety

#### Scenario: unsafe context is rejected

- **WHEN** anonymous unmap or protection-change logic is reached from IRQ context, a preemption-disabled scheduler critical section, or another context that cannot safely allocate or modify the current process address space
- **THEN** BigOS MUST return a deterministic error or enter a documented diagnostic path
- **AND** it MUST NOT free the active kernel stack, tear down the active CR3 root, perform blocking work, or publish partial page-table changes
