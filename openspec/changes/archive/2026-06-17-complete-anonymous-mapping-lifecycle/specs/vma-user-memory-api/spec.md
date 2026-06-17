## ADDED Requirements

### Requirement: 匿名映射支持主动解除映射

BigOS SHALL provide a bounded anonymous unmap operation over the existing VMA model. The operation MUST accept only page-aligned, non-empty user low-half ranges that are completely covered by anonymous/private VMAs compatible with unmap. On success it MUST remove or split affected VMAs, release materialized user pages in the range, update materialization accounting, and leave unrelated VMAs and mappings unchanged. The capability SHALL NOT implement full POSIX `munmap`, `MAP_FIXED` overwrite, file-backed writable unmap semantics, or shared writable mapping semantics.

#### Scenario: 完整匿名 VMA 被解除映射

- **WHEN** a process requests unmap for a page-aligned range that exactly covers an anonymous/private VMA
- **THEN** BigOS MUST remove that VMA from the process VMA collection
- **AND** it MUST unmap every materialized page in the range, release owned frames or decrement COW/shared frame references, reclaim empty dynamic page-table pages when allowed, and invalidate affected current-CPU translations

#### Scenario: 匿名 VMA 局部解除映射触发拆分

- **WHEN** a process requests unmap for a page-aligned subrange in the middle, prefix, or suffix of an anonymous/private VMA
- **THEN** BigOS MUST update the VMA collection to preserve the still-mapped left and/or right ranges with their original permissions, backing, purpose, growth policy, and materialization accounting
- **AND** the unmapped subrange MUST no longer validate through VMA-backed user range checks

#### Scenario: 非法 unmap 请求被拒绝且无副作用

- **WHEN** an unmap request is unaligned, empty, overflows, enters kernel space, is not fully covered by compatible VMAs, crosses incompatible backing types, or cannot allocate required VMA split metadata
- **THEN** BigOS MUST return a deterministic negative error
- **AND** it MUST leave the pre-call VMA collection, materialized pages, page tables, COW reference counts, and visible process state unchanged

### Requirement: 匿名映射支持有界权限变更

BigOS SHALL provide a bounded protection-change operation over anonymous/private VMAs. The operation MUST accept only page-aligned, non-empty user low-half ranges completely covered by compatible VMAs and supported permissions. It MUST reject W+X, unsupported executable policy, file-backed write upgrades, kernel-space ranges, and incompatible backing types. On success it MUST update VMA permissions and present PTE permissions so that page-table access is no wider than the VMA policy.

#### Scenario: 匿名 VMA 权限被收紧

- **WHEN** a process requests a supported protection change that removes write or execute access from a page-aligned anonymous/private range
- **THEN** BigOS MUST update the affected VMA range permissions, splitting VMAs if necessary
- **AND** all present PTEs in the range MUST be updated to permissions no wider than the new VMA permissions, followed by current-CPU TLB invalidation for affected translations

#### Scenario: 匿名 VMA 权限被有界调整

- **WHEN** a process requests a supported protection change for a compatible anonymous/private range and the operation requires VMA splitting
- **THEN** BigOS MUST preserve unaffected left and/or right VMA ranges with their previous permissions and metadata
- **AND** future demand paging or COW fault handling for the changed range MUST use the new VMA permissions

#### Scenario: 非法 protection change 被拒绝且无副作用

- **WHEN** a protection-change request is unaligned, empty, overflows, enters kernel space, requests W+X, upgrades a read-only file-backed range to writable, crosses incompatible VMAs, or cannot allocate required split metadata
- **THEN** BigOS MUST return a deterministic negative error
- **AND** it MUST leave the pre-call VMA collection, materialized pages, PTE permissions, COW markers, reference counts, and visible process state unchanged

### Requirement: VMA range operations preserve materialization accounting

BigOS SHALL keep VMA materialization accounting consistent across anonymous unmap and protection-change operations. Unmapped lazy intervals MUST be removed from metadata without materializing pages. Unmapped materialized pages MUST be released according to ownership or COW reference rules. Protection changes MUST preserve which pages are materialized while updating the permissions that future materialization uses.

#### Scenario: unmap 删除未物化 lazy 区间

- **WHEN** unmap covers a registered-but-unmaterialized portion of an anonymous VMA
- **THEN** BigOS MUST remove that range from VMA/materialization metadata without allocating a user frame
- **AND** later access to that virtual range MUST fail through VMA validation or the documented user fault path rather than silently materializing memory

#### Scenario: protection change 保留物化状态

- **WHEN** protection change covers a mix of materialized and unmaterialized anonymous pages
- **THEN** BigOS MUST preserve the materialized/unmaterialized accounting for the range
- **AND** already present pages and future materialized pages MUST both observe the new VMA permissions

### Requirement: 匿名映射生命周期验证可复现

BigOS SHALL provide reproducible validation for anonymous map, unmap, protection change, VMA splitting, rollback, materialization accounting, COW interaction, and user fault behavior behind a default-off validation path that does not remove existing smoke switches or markers.

#### Scenario: 默认关闭的 smoke 覆盖匿名生命周期

- **WHEN** the anonymous mapping lifecycle validation switch is enabled
- **THEN** the smoke MUST exercise successful anonymous map, successful unmap, access-after-unmap deterministic failure, successful permission reduction, write-after-readonly deterministic failure, and illegal request rollback
- **AND** with no smoke switches the lifecycle smoke MUST stay off and existing userland, demand-paging, fork/COW, and file-backed mapping smokes MUST remain available

#### Scenario: 源码级检查覆盖拆分与回滚

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover VMA prefix/suffix/middle split, capacity exhaustion rollback, unsupported permission rejection, materialization accounting after unmap, and COW reference handling
- **AND** checks MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, and exception/IRQ EOI semantics are not moved or widened
