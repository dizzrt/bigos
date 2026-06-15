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
