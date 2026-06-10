## Purpose

定义 BigOS 进程复制与写时复制能力：以 `int 0x80` 新增的 `SYS_FORK` 在父进程上下文复制出子进程；通过 COW 复制用户地址空间（共享可写匿名物理帧并降权为只读，避免逐页深拷贝）；为被共享的用户物理帧引入引用计数以保证安全归还；在统一用户缺页处理中新增 COW 写时分裂分支；复制每进程 fd 表；并为内核内存分配失败与写分裂失败定义确定性降级语义。该能力以单核、同步、无信号为前提，不引入 `vfork`/`clone` 标志、shared/file-backed 映射、swap、page cache、SMP 并发引用计数或用户态 libc。

## Requirements

### Requirement: SYS_FORK 父子复制语义

BigOS SHALL provide a `SYS_FORK` syscall on the existing `int 0x80` entry that duplicates the current user process into a new child process. The child MUST receive a copy-on-write copy of the parent address space, a copied per-process fd table, an independent PID, and an independent kernel stack. On success the parent MUST observe the child PID and the child MUST observe `0` through the syscall return register. `fork` MUST NOT change the existing `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention, the syscall vector, or the no-EOI syscall rule.

#### Scenario: fork 返回值区分父子

- **WHEN** a Running CPL3 process invokes `SYS_FORK` from ordinary syscall context where allocation and blocking are permitted
- **THEN** BigOS MUST create a child process with a fresh PID, link it as a child of the caller, and return the child PID to the parent and `0` to the child through the syscall return register
- **AND** both parent and child MUST resume at the instruction following the `fork` syscall with identical user register state except the return value

#### Scenario: fork 子进程纳入既有生命周期

- **WHEN** a child created by `SYS_FORK` exits or faults
- **THEN** the child MUST flow through the existing `exit`/fault, zombie, and reaper teardown path, and the parent MUST be able to `wait` for it and observe its exit status
- **AND** the child's COW-shared frames MUST be released through the reference-counted teardown rather than unconditionally freed

### Requirement: COW 地址空间复制

BigOS SHALL duplicate the parent user address space for `fork` using copy-on-write rather than eager per-page deep copy. For each writable anonymous-backed user page that is currently materialized, BigOS MUST remap both the parent and child page-table entries read-only with a COW marker over the same shared physical frame, and MUST copy the VMA collection (ranges, purposes, backings, growth, permissions, and materialization accounting), the borrowed kernel higher-half entries, and the per-process kernel-stack mapping. Read-only anonymous-backed pages MAY be shared directly (reference-counted, without a COW marker, since they never become writable). ELF-segment-backed pages (including writable data segments) MUST be copied into independent child frames rather than COW-shared in this stage. Unmaterialized lazy ranges MUST be copied as metadata only without forcing materialization.

#### Scenario: 可写匿名页进入 COW 共享

- **WHEN** `fork` copies a materialized writable anonymous-backed user page (heap, anonymous mapping, downward stack, or ELF zero-fill range)
- **THEN** BigOS MUST point both parent and child PTEs at the same physical frame, clear the writable bit, and set the COW marker on both
- **AND** BigOS MUST NOT allocate a new frame or copy page contents at `fork` time for that page

#### Scenario: ELF 段页复制为独立子帧

- **WHEN** `fork` copies a materialized ELF-segment-backed page (text, read-only data, or writable data)
- **THEN** BigOS MUST allocate an independent child frame, copy the parent page contents, and map it into the child with the segment permissions, without COW-sharing or setting the COW marker
- **AND** the child frame MUST be reference-counted with an initial count of one so teardown follows the unified reference-counted release path

#### Scenario: 未物化惰性区间只复制元数据

- **WHEN** `fork` encounters a registered-but-unmaterialized lazy range in the parent VMA collection
- **THEN** BigOS MUST copy the VMA metadata and materialization accounting into the child without allocating or mapping a physical frame
- **AND** first access in either process MUST later materialize through the existing unified page-fault path

#### Scenario: 内核借用项与内核栈被正确复制

- **WHEN** `fork` derives the child address-space root
- **THEN** BigOS MUST copy the kernel higher-half top-level entries (kernel image, self-mapping, direct map, KVMEM) as borrowed entries and establish an independent per-process kernel-stack mapping for the child
- **AND** the child root MUST NOT share user low-half page-table ownership with the parent beyond the intended COW leaf frames

### Requirement: 用户物理帧引用计数

BigOS SHALL maintain a reference count for user physical frames shared by copy-on-write. The count MUST be incremented when a frame becomes shared at `fork`, decremented on write-time split and on address-space teardown, and the frame MUST be returned to the buddy allocator only when its count reaches zero. Reference counting MUST keep parent and child teardown order-independent so that whichever process exits first never frees a frame still mapped by the other.

#### Scenario: 父子任一方先退出不过早释放

- **WHEN** one of two processes sharing a COW frame is reaped while the other still maps that frame
- **THEN** teardown MUST decrement the frame reference count and MUST NOT return the frame to buddy while the count remains above zero
- **AND** the surviving process MUST continue to read the frame contents correctly

#### Scenario: 计数归零才归还 buddy

- **WHEN** the last process referencing a COW frame is torn down or splits the page
- **THEN** the reference count MUST reach zero and the frame MUST then be returned to the buddy allocator exactly once
- **AND** no frame MUST be double-freed or leaked across the fork/split/teardown sequence

### Requirement: COW 写时分裂缺页处理

BigOS SHALL extend the unified user page-fault handler with a copy-on-write split branch. When a CPL3 write fault hits a present, read-only, COW-marked, writable anonymous-backed page, BigOS MUST resolve it by either splitting (allocating a new frame, copying the original page contents, remapping the faulting process's page writable with the VMA's permissions, and decrementing the original frame reference count) or, when the original frame reference count is already one, restoring write permission in place without allocating a new frame. The faulting user instruction MUST resume successfully after the split.

#### Scenario: 写共享 COW 页触发分裂

- **WHEN** a CPL3 write faults on a present, read-only, COW-marked writable anonymous page whose shared frame reference count is greater than one
- **THEN** BigOS MUST allocate a new frame, copy the original contents, remap the faulting page writable with the VMA permissions for the current process only, and decrement the original frame reference count
- **AND** the other sharing process MUST keep mapping the original frame and MUST NOT observe the writer's modification

#### Scenario: 独占 COW 页原地恢复可写

- **WHEN** a CPL3 write faults on a COW-marked page whose shared frame reference count has dropped to one
- **THEN** BigOS MAY restore the writable bit and clear the COW marker in place without allocating a new frame
- **AND** the page contents MUST be preserved and the faulting instruction MUST resume

#### Scenario: 写分裂分配失败确定性 kill

- **WHEN** a COW write split needs a new frame but frame allocation fails
- **THEN** BigOS MUST terminate the faulting process through the existing fault-to-lifecycle path
- **AND** it MUST NOT panic the kernel, MUST NOT corrupt the shared frame, and MUST NOT leave a partial or writable mapping for the faulting page

### Requirement: fork fd 表复制

BigOS SHALL give the child created by `SYS_FORK` an independent copy of the parent per-process fd table. The copy MUST duplicate the fd entries and their `close_on_exec` and readable flags while sharing the underlying read-only `vfs::File` objects, so closing or replacing a descriptor in one process does not affect the other's descriptor slots. fd table copy MUST respect the growable fd table soft limit.

#### Scenario: 子进程获得独立 fd 表副本

- **WHEN** `fork` copies a parent fd table that has open descriptors
- **THEN** the child MUST have its own fd slots referencing the same `vfs::File` objects with the same `close_on_exec` and readable flags
- **AND** a later `close` in the parent MUST NOT remove the descriptor from the child's fd table

#### Scenario: fd 表复制保持只读 VFS 语义

- **WHEN** parent and child both hold a descriptor to the same file after `fork`
- **THEN** both MUST observe the existing read-only `open`/`read`/`close` semantics unchanged
- **AND** `fork` MUST NOT introduce writable file access, shared write offsets, or new VFS mutation paths

### Requirement: fork 确定性失败与回滚

BigOS SHALL define deterministic failure for `fork`. When PID allocation, process-object allocation, child address-space root derivation, page-table copy, reference-count bookkeeping, or fd-table copy fails, BigOS MUST roll back any partially established child state, free or decrement any frames or tables it allocated, return a deterministic negative error (such as `-bigos::ENOMEM` or `-bigos::EAGAIN`) to the parent, and leave the parent fully runnable. `fork` MUST NOT panic the kernel for these failures and MUST NOT publish a half-constructed child into the process registry or wait/reap chains.

#### Scenario: 复制中途分配失败回滚

- **WHEN** any kernel allocation during `fork` (PID, process object, page-table page, fd table, or shared-frame bookkeeping) fails
- **THEN** BigOS MUST undo the partial child construction, release/decrement everything it allocated for the child, and return a deterministic negative error to the parent
- **AND** the parent MUST remain Running with its address space, VMAs, fd table, and COW state intact

#### Scenario: 达到进程软上限确定性失败

- **WHEN** `fork` would exceed the growable process registry soft limit
- **THEN** BigOS MUST return a deterministic negative error to the parent rather than overflowing or panicking
- **AND** no child PID or process object MUST be leaked

### Requirement: fork/COW 验证可复现

BigOS SHALL provide reproducible validation for `SYS_FORK`, COW address-space copy, frame reference counting, write-time split, fd-table copy, and deterministic failure, behind a default-off switch that emits fixed COM1/VGA markers and does not delete the existing demand-paging, user-program, or growable-tables smoke matrix.

#### Scenario: 默认关闭的 fork_cow_smoke 发射 marker

- **WHEN** the kernel is built with `fork_cow_smoke` enabled (`BIGOS_FORK_COW_SMOKE`)
- **THEN** the smoke MUST exercise fork parent/child execution, write-time split memory isolation, reference-count release on ordered parent/child exit, and allocation-failure deterministic degradation, and emit `BIGOS_FORK_COW_PASSED` or `BIGOS_FORK_COW_FAILED` on COM1/VGA
- **AND** with no smoke switches the fork/COW smoke MUST stay off and the existing `demand_paging_smoke`, `user_*_smoke`, and `growable_tables_smoke` cases MUST remain available

#### Scenario: build 和 smoke 验证记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` cross-toolchain build, relevant `uv run pytest` source-level checks, strict OpenSpec validation for this change, and any QEMU headless serial-marker smoke used for fork/COW behavior
- **AND** it MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, and exception/IRQ EOI semantics are not moved or widened

### Requirement: fork 子进程继承父进程身份字段

BigOS SHALL 在 `fork` 复制进程时，让子进程逐字段继承父进程的身份四元组 uid/gid/euid/egid，不改变既有 COW 地址空间复制、引用计数与失败回滚语义。

#### Scenario: 子进程身份等于父进程

- **WHEN** 父进程通过 `fork_current` 复制出子进程
- **THEN** 子进程的 uid/gid/euid/egid MUST 逐字段等于父进程对应值

#### Scenario: 身份继承不影响既有复制语义

- **WHEN** `fork` 在复制身份字段的同时执行 COW 地址空间复制、fd 表复制与引用计数
- **THEN** 既有 COW 复制、用户物理帧引用计数与确定性失败回滚语义 MUST 保持不变
- **AND** 身份字段复制 MUST NOT 引入额外的内存分配失败点或改变父进程返回子 PID、子进程返回 0 的约定

### Requirement: fork 继承信号处置与掩码并清空 pending

BigOS SHALL 在 `fork` 复制进程时，让子进程逐字段继承父进程的每信号处置表与阻塞掩码，并把子进程的 pending 信号集清空，且不改变既有 COW 地址空间复制、引用计数、失败回滚与「父返回子 PID、子返回 0」语义。

#### Scenario: 子进程继承处置表与掩码

- **WHEN** 父进程通过 `fork` 复制出子进程
- **THEN** 子进程的每信号处置表与阻塞掩码 MUST 逐字段等于父进程对应值
- **AND** 该继承 MUST NOT 引入新的分配失败路径

#### Scenario: 子进程 pending 信号集清空

- **WHEN** 父进程通过 `fork` 复制出子进程
- **THEN** 子进程的 pending 信号位图 MUST 为空（不继承父进程未投递的 pending 信号）
- **AND** fork 既有的 COW 复制、引用计数、失败回滚与父子返回值语义 MUST 保持不变
