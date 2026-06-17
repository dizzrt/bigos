## Purpose

定义 BigOS 用户态按需分页能力：将所有 CPL3 缺页统一路由到单一用户缺页处理入口，由覆盖该页的 VMA 用途、backing、增长策略与权限决定恢复方式；对匿名 backing（匿名映射、heap、向下增长栈、ELF 零填充范围）执行惰性零页物化；对不可恢复的用户缺页定义确定性失败语义并经现有 fault-to-lifecycle 路径终止进程；同时保留内核态缺页的诊断 panic 边界。该能力不引入 file-backed 映射、shared mapping、swap、page cache、COW 或完整 POSIX `mmap` 语义。

## Requirements

### Requirement: 统一用户态缺页处理入口

BigOS SHALL route all CPL3 page faults through a single user page-fault handling entry that locates the VMA covering the faulting page and decides recovery from VMA purpose, backing, growth, and permissions, rather than recognizing only stack VMAs. The unified entry MUST replace the stack-only recovery path as the recovery hook called by the `#PF` handler, and MUST keep CPL0 (kernel-mode) page faults on the existing diagnostic panic/halt path.

#### Scenario: 用户缺页交由统一入口判定

- **WHEN** a CPL3 page fault occurs while a current process is Running and the context can allocate safely
- **THEN** BigOS MUST locate the VMA covering the faulting page and select a recovery decision from that VMA's purpose, backing, growth, and permissions
- **AND** it MUST NOT special-case only the stack VMA while ignoring other lazily-registered anonymous ranges

#### Scenario: 内核态缺页仍 panic

- **WHEN** a page fault occurs in CPL0 / kernel context, or in a context that cannot allocate safely
- **THEN** BigOS MUST preserve the existing kernel diagnostic behavior, emitting the `BIGOS_PAGE_FAULT` diagnostic and halting
- **AND** it MUST NOT attempt anonymous demand-zero materialization for kernel-mode faults

### Requirement: 匿名 backing 惰性零页物化

BigOS SHALL materialize anonymous-backed user pages lazily on first access. When a CPL3 fault hits a registered-but-unmaterialized page of an anonymous-backed VMA (anonymous mapping, heap, downward stack, or ELF zero-fill range) with access compatible with the VMA permissions, BigOS MUST allocate a user frame, zero it, install the user page-table entry with the VMA's permissions, advance the VMA materialization accounting, and return to the faulting instruction.

#### Scenario: 首次访问匿名页惰性物化

- **WHEN** a CPL3 access faults on a not-present page within an anonymous-backed VMA, the access is permitted by the VMA permissions, and a user frame is available
- **THEN** BigOS MUST allocate and zero one user frame, map it for the faulting page with the VMA's user permissions (writable pages non-executable), and advance the VMA's `materialized_start`/`materialized_end` accordingly
- **AND** the faulting user instruction MUST resume successfully after recovery

#### Scenario: 向下栈增长作为统一入口的一个分支

- **WHEN** a CPL3 fault occurs within a downward-growth stack VMA, below the current materialized start and above the stack limit, with stack-compatible access
- **THEN** BigOS MUST materialize the required stack page through the same unified anonymous materialization path and advance `materialized_start`
- **AND** the recovery MUST remain consistent with the prior stack-growth semantics

### Requirement: 缺页确定性失败语义

BigOS SHALL define deterministic failure for unrecoverable user faults. When a CPL3 fault is on a legitimate anonymous page but frame allocation fails, OR the access violates VMA permissions, is out of any VMA range, hits a non-anonymous backing without a recovery policy, sets the present (protection-violation) bit, or occurs in a non-blocking context, BigOS MUST terminate the current user process through the existing fault-to-lifecycle path. It MUST NOT panic the kernel for these CPL3 faults and MUST NOT silently leave a partial mapping.

#### Scenario: 分配失败确定性 kill

- **WHEN** a recoverable CPL3 anonymous fault cannot obtain a user frame
- **THEN** BigOS MUST terminate the current process deterministically through the documented user fault path
- **AND** it MUST NOT panic the kernel and MUST NOT publish a partial page-table mapping for the faulting page

#### Scenario: 权限违例或越界 kill

- **WHEN** a CPL3 fault requests access incompatible with the covering VMA permissions, falls outside any VMA, sets the present/protection-violation bit, or targets a non-anonymous backing lacking a recovery policy
- **THEN** BigOS MUST terminate the current process through the documented user fault path
- **AND** it MUST NOT convert the fault into a successful materialization

### Requirement: 按需分页验证可复现

BigOS SHALL provide reproducible validation for the unified page-fault handler, lazy anonymous materialization, allocation-failure kill, permission-violation kill, and kernel-fault panic boundary, behind a default-off switch that emits fixed COM1/VGA markers and does not delete the existing page-fault or user-program smoke matrix.

#### Scenario: 默认关闭的 demand_paging_smoke 发射 marker

- **WHEN** the kernel is built with `demand_paging_smoke` enabled (`BIGOS_DEMAND_PAGING_SMOKE`)
- **THEN** the smoke MUST exercise lazy-materialization hit, allocation-failure kill, and permission-violation kill, and emit `BIGOS_DEMAND_PAGING_PASSED` or `BIGOS_DEMAND_PAGING_FAILED` on COM1/VGA
- **AND** with no smoke switches the demand-paging smoke MUST stay off and the existing `page_fault_smoke` and `user_*_smoke` cases MUST remain available

#### Scenario: build 和 smoke 验证记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` cross-toolchain build, relevant `uv run pytest` source-level checks, strict OpenSpec validation for this change, and any QEMU headless serial-marker smoke used for demand-paging behavior
- **AND** it MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, and exception/IRQ EOI semantics are not moved or widened

### Requirement: COW 写错误分支

BigOS SHALL extend the unified user page-fault handler with a copy-on-write write-fault branch that is distinct from the existing protection-violation kill rule. When a CPL3 fault has the present (protection-violation) bit set but targets a read-only, COW-marked, writable anonymous-backed page, BigOS MUST treat it as a copy-on-write split candidate rather than an unconditional permission-violation kill. All other present-bit faults (non-COW protection violations) MUST keep the existing deterministic kill semantics.

#### Scenario: present 位 COW 页不再直接 kill

- **WHEN** a CPL3 write fault sets the present bit on a read-only page that carries the COW marker and lies in a writable anonymous-backed VMA
- **THEN** BigOS MUST route the fault to the copy-on-write split branch instead of terminating the process as a permission violation
- **AND** after the split or in-place re-enable the faulting instruction MUST resume successfully

#### Scenario: 非 COW present 违例仍 kill

- **WHEN** a CPL3 fault sets the present bit on a page that is not COW-marked, or requests access incompatible with the covering VMA permissions
- **THEN** BigOS MUST preserve the existing deterministic kill through the documented user fault path
- **AND** it MUST NOT convert a genuine protection violation into a successful copy-on-write materialization

### Requirement: 缺页恢复遵守 runtime layout

BigOS SHALL require the unified user page-fault handler to validate the faulting address against the committed runtime VM layout before materializing memory. A fault in a guard range, reserved future-runtime gap, kernel range, unsupported file-backed range, or out-of-layout address MUST NOT be recovered as anonymous memory.

#### Scenario: recoverable anonymous fault is in layout

- **WHEN** a CPL3 not-present fault targets a lazily backed heap, restricted anonymous, stack-growth, or ELF zero-fill page
- **THEN** BigOS MUST confirm the faulting page is covered by a compatible VMA and allowed by the committed runtime layout before allocating and mapping a user frame
- **AND** the mapped page MUST use permissions no wider than the VMA and layout allow

#### Scenario: reserved or guard fault is not materialized

- **WHEN** a CPL3 fault targets a runtime-reserved gap, stack guard region, kernel range, unsupported file-backed range, or address outside the committed runtime layout
- **THEN** BigOS MUST terminate the current user process through the documented user fault path
- **AND** it MUST NOT silently convert the fault into a successful anonymous materialization

### Requirement: COW faults preserve runtime ownership

BigOS SHALL handle copy-on-write write faults only for pages whose VMA and runtime layout region both permit writable anonymous ownership. COW split or in-place re-enable MUST preserve materialization accounting, frame ownership, and teardown behavior for the affected process.

#### Scenario: valid COW write fault splits owned page

- **WHEN** a CPL3 write fault sets the present bit on a read-only COW-marked page in a writable anonymous-backed runtime layout region
- **THEN** BigOS MUST route the fault to the COW branch, allocate or reuse a frame according to the COW ownership rules, restore writable non-executable user permissions, and resume the faulting instruction
- **AND** the VMA materialization accounting and process-owned frame records MUST remain consistent for later fork, exec, exit, and reap

#### Scenario: invalid COW candidate is killed

- **WHEN** a CPL3 present-bit fault targets a page that is not COW-marked, is outside a writable anonymous runtime region, or violates VMA permissions
- **THEN** BigOS MUST preserve deterministic process termination through the documented user fault path
- **AND** it MUST NOT treat a genuine protection violation as a successful COW split

### Requirement: page-fault validation records layout boundaries

BigOS SHALL validate that demand-paging behavior respects runtime layout boundaries and preserves kernel fault diagnostics.

#### Scenario: validation covers runtime-layout faults

- **WHEN** this change is implemented
- **THEN** validation MUST cover successful lazy materialization inside allowed runtime layout regions and deterministic failure for reserved gaps, guard ranges, permission violations, and invalid COW candidates
- **AND** validation MUST confirm CPL0 page faults remain on the kernel diagnostic/panic path rather than user demand-paging recovery

### Requirement: file-backed 只读物化分支

BigOS SHALL extend the unified user page-fault handler with a file-backed read-only materialization branch that is a controlled exception to the existing rule that terminates CPL3 faults on non-anonymous backing lacking a recovery policy. When a CPL3 not-present read fault hits a registered-but-unmaterialized page of a read-only file-backed VMA, in a context that permits blocking, BigOS MUST materialize the page from the backing file through the page/buffer cache with read-only user permissions rather than terminating the process. All write faults on read-only file-backed pages and all faults outside a recoverable VMA MUST keep the existing deterministic kill semantics.

#### Scenario: not-present 读缺页进入 file-backed 分支

- **WHEN** a CPL3 not-present read fault targets a page within a read-only file-backed VMA, the access is read-only and within the backing file's mappable extent, and the context permits blocking
- **THEN** BigOS MUST route the fault to the file-backed read-only materialization branch, read the covering file block(s) through the page/buffer cache, install a read-only non-executable user page-table entry, and advance the VMA materialization accounting
- **AND** the faulting instruction MUST resume successfully observing the file content

#### Scenario: file-backed 写缺页或越界仍 kill

- **WHEN** a CPL3 fault requests write access to a read-only file-backed page, targets an address outside the mapped file-backed range, or occurs in a non-blocking context
- **THEN** BigOS MUST preserve deterministic termination through the documented user fault path
- **AND** it MUST NOT convert the fault into a successful file-backed materialization or enter copy-on-write

#### Scenario: 内核态缺页仍 panic

- **WHEN** a page fault occurs in CPL0 / kernel context
- **THEN** BigOS MUST preserve the existing kernel diagnostic/panic behavior
- **AND** it MUST NOT attempt file-backed materialization for kernel-mode faults
