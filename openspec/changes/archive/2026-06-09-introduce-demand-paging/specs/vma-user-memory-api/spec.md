## MODIFIED Requirements

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
