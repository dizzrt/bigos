## Purpose

定义 BigOS VMA-backed 用户内存 API 能力：为每个用户进程镜像维护有界 VMA 集合，以 VMA 作为用户虚拟内存策略来源，并提供最小 `brk`、受限匿名映射、VMA-backed 用户范围校验、受限用户栈增长与可复现验证边界。该能力不引入 file-backed mmap、shared mapping、swap、page cache、通用 demand paging 或完整 POSIX `mmap` 语义。

## Requirements

### Requirement: 进程拥有有界 VMA 集合

BigOS SHALL maintain a bounded VMA collection for each user process image. Each VMA MUST describe a user low-half virtual range, access permissions, purpose, backing type, growth policy, and ownership needed to validate user memory operations without relying only on page-table probing.

#### Scenario: exec commit 发布 VMA 集合

- **WHEN** a bounded ELF64 exec path commits a new user image
- **THEN** BigOS MUST publish VMAs for executable segments, writable data/BSS, initial stack, stack guard/growth range, and optional heap boundary before user execution begins
- **AND** every published VMA MUST stay within the supported user low-half range and avoid kernel higher-half, direct-map, KVMEM, and recursive self-mapping ranges

#### Scenario: VMA 重叠被拒绝

- **WHEN** process image preparation, `brk`, or anonymous mapping would create a VMA that overlaps an existing VMA with incompatible ownership or permissions
- **THEN** BigOS MUST reject the operation deterministically before publishing the new VMA
- **AND** it MUST leave the previous process image and VMA collection unchanged unless the process has already entered a documented commit-failure termination path

#### Scenario: VMA 容量耗尽失败可回滚

- **WHEN** a process image or user memory API needs another VMA but the bounded VMA collection is full
- **THEN** BigOS MUST fail the operation with a deterministic error or terminate the affected process through the documented lifecycle path
- **AND** any pages, page-table entries, or metadata allocated by the failed attempt MUST be released or scheduled for safe release

### Requirement: brk 管理进程 heap VMA

BigOS SHALL provide a minimal `brk` capability that changes the current process heap break only within a bounded heap VMA. The operation MUST validate alignment, bounds, collisions, and rollback behavior before exposing the new break to user mode. Heap growth MUST register a lazy interval (updating heap break and VMA/materialization metadata) rather than eagerly allocating and mapping every newly covered page; the covered pages are materialized on first access through the unified demand-paging path. Heap shrink and teardown MUST unmap only pages that were actually materialized, according to the heap VMA materialization accounting.

#### Scenario: brk 扩展 heap

- **WHEN** a user process requests a new break above the current break and within the heap VMA limit
- **THEN** BigOS MUST update the heap break and heap VMA metadata to register the newly covered range as lazily backed, without requiring eager allocation of every newly covered page
- **AND** the new break MUST become visible only after all required VMA and metadata updates succeed, and subsequent first access to a covered page MUST be materialized with user writable non-executable permissions through the unified demand-paging path

#### Scenario: brk 收缩 heap

- **WHEN** a user process requests a new break below the current break and not below the heap base
- **THEN** BigOS MUST unmap only the heap pages that were actually materialized and are no longer covered by the break, preserve remaining heap mappings, and update page-table ownership and materialization accounting consistently
- **AND** the returned break MUST reflect the committed heap boundary

#### Scenario: brk 失败保持旧边界

- **WHEN** `brk` receives an out-of-range address, overlaps another VMA, overflows address arithmetic, or cannot allocate required metadata
- **THEN** BigOS MUST return a deterministic error or old break according to the documented ABI
- **AND** the process heap VMA, mapped heap pages, materialization accounting, and visible break MUST remain at the pre-call state

### Requirement: 受限匿名用户映射

BigOS SHALL provide a restricted anonymous user mapping capability for bounded, page-aligned, non-file-backed mappings. The capability SHALL NOT implement file-backed mapping, shared mapping, overwrite-on-fixed mapping, swap, page cache, or full POSIX `mmap` semantics. A successful anonymous mapping MUST register a non-overlapping user VMA as lazily backed rather than eagerly allocating and mapping every page; covered pages are materialized on first access through the unified demand-paging path.

#### Scenario: anonymous mapping 创建私有用户页

- **WHEN** a process requests a bounded anonymous mapping with supported permissions
- **THEN** BigOS MUST reserve a non-overlapping user VMA, register its range as lazily backed, and return the mapped user address range without requiring eager allocation of every page
- **AND** writable mappings MUST be non-executable unless a later explicit executable policy allows otherwise, and first access to a covered page MUST be materialized with the requested permissions through the unified demand-paging path

#### Scenario: unsupported mapping 被拒绝

- **WHEN** an anonymous mapping request includes unsupported flags, file-backed state, shared semantics, kernel-space addresses, W+X permissions, or a range that collides with existing VMAs
- **THEN** BigOS MUST reject the request deterministically
- **AND** it MUST NOT publish partial VMA metadata or partial user mappings as a successful operation

### Requirement: 用户范围验证基于 VMA

BigOS SHALL validate user-provided ranges by checking VMA coverage, requested access permissions, user low-half bounds, overflow, and page-table accessibility before kernel code copies from or to user memory.

#### Scenario: valid user read range

- **WHEN** syscall handling needs to read bytes from a user pointer and length
- **THEN** BigOS MUST confirm the complete range is covered by VMAs that allow user read access and does not overflow or enter kernel address ranges
- **AND** it MUST confirm present user-accessible mappings or use an equivalent safe-copy path that detects invalid mappings before treating the read as successful

#### Scenario: valid user write range

- **WHEN** syscall handling needs to write bytes into a user destination range
- **THEN** BigOS MUST confirm the complete range is covered by VMAs that allow user write access
- **AND** it MUST reject or terminate on unmapped, read-only, executable-only, kernel-space, or overflowed ranges before corrupting kernel or unrelated process memory

#### Scenario: invalid user range fails deterministically

- **WHEN** a user pointer range is not fully covered by a compatible VMA or fails page-table accessibility checks
- **THEN** BigOS MUST return a deterministic negative error or terminate the current user process through the documented user fault path
- **AND** it MUST NOT read or write arbitrary kernel memory

### Requirement: 用户栈增长受 VMA 限制

BigOS SHALL define stack guard and stack-growth VMAs for user stacks. A user page fault MAY be recovered only when it is a CPL3 access that matches the current process stack-growth policy; all other user page faults MUST follow the existing fault-to-lifecycle termination behavior. Stack-growth recovery MUST be handled as one branch of the unified demand-paging entry rather than a separate stack-only handler, reusing the shared anonymous materialization path.

#### Scenario: stack-growth fault 映射新栈页

- **WHEN** a CPL3 page fault occurs within the current process stack-growth range, below the current materialized stack, above the stack maximum limit, and with access compatible with stack permissions
- **THEN** BigOS MUST allocate and map the required user stack page with writable non-executable user permissions through the unified demand-paging path
- **AND** it MUST update stack VMA/materialized metadata before returning to the faulting user instruction

#### Scenario: 非 stack fault 不恢复

- **WHEN** a page fault occurs outside a recoverable anonymous VMA range, violates guard limits, requests incompatible access, happens in CPL0, or happens in a context that cannot allocate safely
- **THEN** BigOS MUST preserve kernel-mode diagnostic behavior for CPL0 faults and MUST terminate or mark faulted the current user process for CPL3 faults
- **AND** it MUST NOT silently convert an unrecoverable fault into a successful materialization

### Requirement: VMA 用户内存 API 验证可复现

BigOS SHALL provide reproducible validation for VMA ownership, `brk`, anonymous mappings, VMA-backed user range validation, stack-growth policy, failure rollback, and lifecycle cleanup.

#### Scenario: source checks 覆盖 VMA 不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover non-overlapping VMA insertion, permission checks, capacity exhaustion, `brk` growth/shrink rollback, anonymous mapping rejection, stack-growth gate conditions, and user-copy VMA validation
- **AND** checks MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, and exception/IRQ EOI semantics are not moved or widened

#### Scenario: build 和 smoke 验证记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` cross-toolchain build, relevant `uv run pytest` source-level checks, strict OpenSpec validation for this change, and any QEMU headless serial-marker smoke used for VMA/user-memory behavior
- **AND** if QEMU, Bochs, cross-binutils, ROM/display configuration, serial observability, or disk image generation is unavailable, validation MUST record the skipped case, substitute checks, and residual bootability risk

### Requirement: VMA 集合按地址空间复制

BigOS SHALL support duplicating a process VMA collection and its materialization accounting as part of address-space copy on `fork`. The duplicate MUST preserve every VMA's range, purpose, backing, growth, permissions, and `materialized_start`/`materialized_end` accounting, as well as the heap base/break/limit and anonymous next-cursor bookkeeping, so the child observes the same user address-space layout as the parent at fork time.

#### Scenario: 复制 VMA 集合保留布局与记账

- **WHEN** a process address space is duplicated for `fork`
- **THEN** BigOS MUST copy the full VMA collection including per-VMA materialization accounting and the heap/anonymous bookkeeping fields
- **AND** the child MUST present the same VMA ranges, permissions, and growth policies as the parent

### Requirement: 可写匿名区间 fork 进入 COW 共享

BigOS SHALL make writable anonymous-backed ranges established through `brk` and restricted anonymous mapping enter copy-on-write sharing on `fork` rather than eager deep copy. Materialized writable anonymous pages MUST be shared read-only with a COW marker between parent and child, and unmaterialized lazy intervals MUST be carried as metadata only, to be materialized later through the existing unified page-fault path in whichever process first accesses them.

#### Scenario: brk/匿名区间 fork 时不深拷贝

- **WHEN** `fork` duplicates a process whose heap or anonymous mapping has materialized writable pages
- **THEN** BigOS MUST share those frames copy-on-write read-only between parent and child instead of allocating and copying new frames at fork time
- **AND** unmaterialized portions of the same intervals MUST be duplicated as VMA metadata without forcing materialization

### Requirement: VMA purpose matches runtime layout region

BigOS SHALL require each user VMA to declare a purpose and backing that matches exactly one allowed region class in the committed runtime VM layout. VMA insertion for exec, `brk`, restricted anonymous mapping, stack growth, or argument setup MUST reject ranges that collide with reserved gaps or incompatible region purposes.

#### Scenario: VMA insertion validates region purpose

- **WHEN** process image preparation or a user memory API creates a VMA
- **THEN** BigOS MUST validate that the requested range belongs to an allowed runtime layout region and that the VMA purpose, permissions, backing type, and growth policy match that region
- **AND** the VMA MUST NOT overlap another VMA or a reserved future-runtime gap

#### Scenario: incompatible VMA request fails without publication

- **WHEN** a `brk`, restricted anonymous mapping, stack-growth registration, or exec image setup request would create a VMA in an incompatible runtime layout region
- **THEN** BigOS MUST reject the operation deterministically
- **AND** it MUST leave the previous VMA collection, mapped pages, materialization accounting, and visible process state unchanged unless the process has entered a documented fatal path

### Requirement: materialization accounting is shared by VM operations

BigOS SHALL use one materialization accounting model for lazily backed heap, restricted anonymous mappings, downward-growing stack pages, and ELF zero-fill ranges. The accounting MUST be sufficient for demand paging, fork/COW duplication, range validation, shrink/unmap, exec replacement, and process teardown.

#### Scenario: lazy-backed regions share accounting

- **WHEN** heap, anonymous mapping, stack, or ELF zero-fill memory is registered as lazily backed
- **THEN** BigOS MUST record enough VMA metadata to distinguish registered-but-unmaterialized pages from materialized pages
- **AND** first access, fork/COW, shrink, unmap, and teardown MUST update or consume that metadata consistently

#### Scenario: accounting failure rolls back operation

- **WHEN** a VM operation cannot allocate or update required materialization accounting
- **THEN** BigOS MUST fail the operation before exposing the new layout or mapping to user mode
- **AND** any pages, PTEs, or VMA records from the failed operation MUST be released or routed through the documented safe-release path

### Requirement: user range validation respects runtime layout

BigOS SHALL validate user-provided ranges against both the VMA collection and the committed runtime layout before copying data between kernel and user memory. A present PTE alone MUST NOT authorize access if the range is outside the runtime layout or violates the VMA purpose and permissions.

#### Scenario: user copy rejects reserved gap

- **WHEN** syscall handling needs to read from or write to a user pointer range that overlaps a runtime-reserved gap
- **THEN** BigOS MUST reject the range or terminate the current user process through the documented user fault path
- **AND** it MUST NOT read or write arbitrary kernel memory or materialize the reserved gap

#### Scenario: user copy accepts compatible materialized range

- **WHEN** a user pointer range is fully covered by compatible VMAs, allowed by the runtime layout, and backed by present user-accessible mappings or an equivalent safe-copy path
- **THEN** BigOS MUST allow the bounded copy according to the requested access mode
- **AND** the copy MUST NOT grant write access to read-only, executable-only, guard, or reserved ranges

### Requirement: VMA 支持 file-backed 只读 backing 类型

BigOS SHALL extend the bounded VMA model with a read-only file-backed backing type that records a backing file reference and a starting file offset in addition to range, permissions, purpose, growth policy, and materialization accounting. VMA range validation, user-range copy validation, `fork` duplication, exec replacement, and process teardown MUST recognize file-backed VMAs and preserve their read-only ownership.

#### Scenario: file-backed VMA 记录文件引用与偏移

- **WHEN** a read-only file-backed mapping is published
- **THEN** the VMA MUST record the backing file reference, the starting file offset, a read-only permission set, and lazy materialization accounting sufficient to distinguish materialized from unmaterialized pages
- **AND** the VMA MUST remain within the supported user low-half range and avoid kernel higher-half, direct-map, KVMEM, and recursive self-mapping ranges

#### Scenario: 用户范围校验识别 file-backed 区间

- **WHEN** syscall handling validates a user range that overlaps a read-only file-backed VMA
- **THEN** BigOS MUST allow read access covered by present user-accessible mappings or an equivalent safe-copy path, and MUST reject write access to the read-only file-backed range
- **AND** it MUST NOT treat a present PTE alone as authorization for write access to a read-only file-backed page

#### Scenario: teardown 释放 file-backed VMA 不误删共享缓存

- **WHEN** a process holding file-backed VMAs exits or is reaped, or its image is replaced by exec
- **THEN** BigOS MUST release the file-backed VMA metadata and the process-owned page-table entries consistently
- **AND** it MUST NOT corrupt or prematurely free shared read-only page/buffer cache state still referenced by other processes

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
