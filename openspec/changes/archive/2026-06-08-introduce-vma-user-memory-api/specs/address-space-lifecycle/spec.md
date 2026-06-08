## ADDED Requirements

### Requirement: 地址空间 teardown 释放 VMA 元数据

BigOS SHALL release process-owned VMA metadata as part of safe user address-space teardown. VMA metadata cleanup MUST be ordered with user leaf page release, dynamic page-table reclamation, user PML4 release, and process object reaping so that no active path observes freed metadata.

#### Scenario: teardown 在安全上下文释放 VMA

- **WHEN** a terminated, faulted, exec-replaced, or reaped process image reaches the safe address-space teardown boundary
- **THEN** BigOS MUST make the process VMA collection unreachable from future user-memory validation before releasing its metadata
- **AND** teardown MUST run only after the active execution path no longer depends on that process VMA collection for syscall, exception, or user-copy handling

#### Scenario: VMA cleanup 不释放借用高半区

- **WHEN** VMA cleanup runs for a user process
- **THEN** BigOS MUST release only user process VMA metadata and user-owned low-half resources associated with those VMAs
- **AND** it MUST NOT treat borrowed kernel higher-half mappings, direct map entries, KVMEM mappings, recursive self-mapping entries, or boot handoff page tables as VMA-owned resources

### Requirement: VMA rollback 与页表 rollback 一致

BigOS SHALL keep VMA rollback consistent with page-table and physical-page rollback for failed `exec`, `brk`, anonymous mapping, and stack-growth operations.

#### Scenario: commit 前失败释放 staging VMA

- **WHEN** exec image preparation fails before the new image is committed
- **THEN** BigOS MUST release staging VMAs, staging user pages, staging dynamic page-table pages, and loader metadata allocated by the failed attempt
- **AND** it MUST preserve the old runnable process image and old VMA collection unchanged

#### Scenario: API 失败撤销已分配资源

- **WHEN** `brk`, anonymous mapping, or stack growth fails after allocating a physical page, page-table page, or VMA slot
- **THEN** BigOS MUST roll back the current operation's allocations and metadata changes or terminate the process through a documented safe teardown path
- **AND** it MUST NOT leave a successful return with VMA and page-table state disagreeing about the affected range
