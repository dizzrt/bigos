## ADDED Requirements

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
