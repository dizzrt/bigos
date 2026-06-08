## ADDED Requirements

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

BigOS SHALL provide a minimal `brk` capability that changes the current process heap break only within a bounded heap VMA. The operation MUST validate alignment, bounds, collisions, allocation failure, and rollback behavior before exposing the new break to user mode.

#### Scenario: brk 扩展 heap

- **WHEN** a user process requests a new break above the current break and within the heap VMA limit
- **THEN** BigOS MUST allocate and map any newly covered heap pages with user writable and non-executable permissions, or use an explicitly documented equivalent eager materialization policy
- **AND** the new break MUST become visible only after all required VMA, page-table, and physical-page updates succeed

#### Scenario: brk 收缩 heap

- **WHEN** a user process requests a new break below the current break and not below the heap base
- **THEN** BigOS MUST unmap heap pages no longer covered by the break, preserve remaining heap mappings, and update page-table ownership accounting consistently
- **AND** the returned break MUST reflect the committed heap boundary

#### Scenario: brk 失败保持旧边界

- **WHEN** `brk` receives an out-of-range address, overlaps another VMA, overflows address arithmetic, or cannot allocate required pages or metadata
- **THEN** BigOS MUST return a deterministic error or old break according to the documented ABI
- **AND** the process heap VMA, mapped heap pages, and visible break MUST remain at the pre-call state

### Requirement: 受限匿名用户映射

BigOS SHALL provide a restricted anonymous user mapping capability for bounded, page-aligned, non-file-backed mappings. The capability SHALL NOT implement file-backed mapping, shared mapping, overwrite-on-fixed mapping, swap, page cache, or full POSIX `mmap` semantics.

#### Scenario: anonymous mapping 创建私有用户页

- **WHEN** a process requests a bounded anonymous mapping with supported permissions
- **THEN** BigOS MUST reserve a non-overlapping user VMA, allocate and map user-owned physical pages according to the current eager policy, and return the mapped user address range
- **AND** writable mappings MUST be non-executable unless a later explicit executable policy allows otherwise

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

BigOS SHALL define stack guard and stack-growth VMAs for user stacks. A user page fault MAY be recovered only when it is a CPL3 access that matches the current process stack-growth policy; all other user page faults MUST follow the existing fault-to-lifecycle termination behavior.

#### Scenario: stack-growth fault 映射新栈页

- **WHEN** a CPL3 page fault occurs within the current process stack-growth range, below the current materialized stack, above the stack maximum limit, and with access compatible with stack permissions
- **THEN** BigOS MAY allocate and map the required user stack page with writable non-executable user permissions
- **AND** it MUST update stack VMA/materialized metadata before returning to the faulting user instruction

#### Scenario: 非 stack fault 不恢复

- **WHEN** a page fault occurs outside the stack-growth VMA, violates guard limits, requests incompatible access, happens in CPL0, or happens in a context that cannot allocate safely
- **THEN** BigOS MUST preserve kernel-mode diagnostic behavior for CPL0 faults and MUST terminate or mark faulted the current user process for CPL3 faults
- **AND** it MUST NOT silently convert the fault into general demand paging

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
