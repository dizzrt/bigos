## Purpose

定义 BigOS 用户地址空间页表准备能力：一个显式的内核虚拟内存 map/unmap primitive
（用统一的 present / writable / user / no-execute / global 页属性驱动 PTE 写入），明确的
user 与 kernel 页属性策略，以及基于内核当前 PML4 派生用户地址空间页表根、共享内核高半区
而隔离用户低半区的最小能力。派生 helper 本身保持 passive，不隐式切换 CR3、不进入 ring3、
不加载用户代码、不实现 demand paging；后续 first-user-program 或进程 runtime MAY 在受控运行路径中
显式激活派生根，并由该 runtime 的验证覆盖激活边界。

## Requirements

### Requirement: 页表映射使用显式页属性

BigOS SHALL 提供一个显式的页属性描述与 map/unmap primitive，用统一的属性 bit
（present、writable、user、no-execute、global）驱动页表条目写入，而不是在多处硬编码裸 PTE 常量。对于运行时动态创建的页表页，primitive 还 MUST 维护足以支持后续空页表页回收的 ownership、level 和 present-entry accounting 或等价不变量。

#### Scenario: map primitive 接受显式属性

- **WHEN** 内核代码在非中断上下文调用页表 map primitive 建立一个虚拟页到物理页的映射
- **THEN** primitive MUST 接受一个显式页属性入参，并据此设置 PTE 的 present、writable、user、
  no-execute bit
- **AND** primitive MUST 在缺少中间页表级时按现有方式分配页表页，并在写入条目时屏蔽 same-CPU
  maskable IRQ 交织
- **AND** primitive MUST register dynamically allocated intermediate page-table pages as reclaimable only when their ownership and level can be tracked

#### Scenario: unmap primitive 清除条目并刷新 TLB

- **WHEN** 内核代码对一个已映射虚拟页调用 unmap primitive
- **THEN** primitive MUST 清除对应 PTE
- **AND** primitive MUST 对该虚拟地址执行 TLB 失效（如 `invlpg`），保持与现有内核 unmap 路径一致的行为
- **AND** if the cleared leaf makes a dynamically owned PT/PD/PDPT empty, primitive or its caller MUST be able to reclaim that empty page-table page without touching non-owned static page tables

#### Scenario: 页表页元数据失败回滚映射

- **WHEN** map primitive needs to allocate a new intermediate page-table page but cannot record the required ownership metadata
- **THEN** primitive MUST fail or stop through a deterministic failure path before publishing a present descriptor for that intermediate page-table page
- **AND** any partially allocated page-table page or partially written descriptor from the current operation MUST be rolled back or left in a documented fatal state

### Requirement: 内核映射保持既有 supervisor 属性

BigOS SHALL 让内核既有页表映射路径经由显式 primitive 表达，并保持当前 PTE bit 行为不变。

#### Scenario: 内核默认属性等价旧常量

- **WHEN** 内核范围（higher-half、KVMEM、direct map 等）通过新 primitive 建立映射
- **THEN** 默认内核属性 MUST 等价于既有 `present + writable`、`user=0` 的 supervisor 语义
- **AND** 源码级检查 MUST 确认内核映射路径不会对内核范围设置 user bit

#### Scenario: 改写不引入内存回归

- **WHEN** 现有 `map_kernel_range()` / `unmap_kernel_range()` 改为经由 primitive 实现
- **THEN** 既有内存运行时 self-test 与内存源码级检查 MUST 仍然通过
- **AND** boot 固定地址、higher-half/load base、`KVMEM_BASE`、direct map 区域与 self-mapping 地址布局
  MUST 保持不变

### Requirement: 用户与内核页属性策略明确

BigOS SHALL 定义并以源码级方式固定 user 与 kernel 映射的页属性策略。

#### Scenario: 用户映射置 user bit 与 NX 策略

- **WHEN** 通过 primitive 建立一个用户态可访问的映射
- **THEN** 该映射 MUST 置 user bit
- **AND** 用户数据页 MUST 置 no-execute bit，用户代码页 MUST 清 no-execute bit

#### Scenario: no-execute 前提被确认或记录

- **WHEN** 实现对页属性编码或写入 no-execute bit
- **THEN** 实现 MUST 确认 `EFER.NXE` 已使能，或在文档/验证记录中明确 NXE 状态与剩余风险
- **AND** 在 NXE 不可用时，验证 MAY 将 no-execute 检查降级为“属性编码正确”而非运行时强制，并记录该降级

### Requirement: 用户地址空间页表根派生共享内核高半区

BigOS SHALL provide a minimal user address-space root derivation capability where the kernel higher half is shared in the derived root and the user lower half is isolated. Derivation by itself SHALL NOT switch CR3 or enter ring3; a later process runtime path MAY explicitly activate the derived root only under its own controlled requirements. Any later teardown of a derived root MUST treat copied kernel higher-half entries as borrowed mappings and MUST NOT reclaim them as process-owned page tables.

#### Scenario: 派生根复制内核高半区条目

- **WHEN** 内核代码基于当前内核 PML4 派生一个新的用户地址空间页表根
- **THEN** 派生根 MUST 复制内核 PML4 高半区顶层条目（覆盖 kernel higher-half、self-mapping、
  direct map、KVMEM 所在区域）
- **AND** 派生根的低半区（用户区）顶层条目 MUST 被清零以保证用户地址空间相互独立
- **AND** copied high-half entries MUST be marked or treated as borrowed kernel mappings that are not owned by the derived user address space

#### Scenario: 派生不隐式切换地址空间

- **WHEN** 用户地址空间页表根被派生
- **THEN** BigOS MUST NOT implicitly write CR3 as part of the derivation helper
- **AND** BigOS MUST NOT implicitly enter ring3, load user code, or implement demand paging as a side effect of deriving the root
- **AND** `#PF` handler MUST remain diagnostic-only for kernel faults

#### Scenario: 进程运行路径可显式激活派生根

- **WHEN** a dedicated first-user-program or process runtime path starts a user process using a derived user address-space root
- **THEN** that runtime path MAY explicitly activate the derived root by switching CR3 or equivalent address-space state
- **AND** the activated root MUST preserve kernel higher-half mappings needed for syscall, exception, IRQ, direct-map, KVMEM, and diagnostic paths
- **AND** this activation MUST be covered by that runtime capability's validation rather than by the derivation helper alone

#### Scenario: teardown 不回收借用高半区

- **WHEN** a later user address-space teardown path releases a derived root
- **THEN** BigOS MUST only reclaim low-half user-owned mappings and dynamically owned low-half page-table pages for that address space
- **AND** it MUST NOT free page-table pages reachable only through copied high-half kernel PML4 entries

### Requirement: 用户映射遵守 VMA 策略

BigOS SHALL ensure user page mappings created after VMA introduction are authorized by a compatible VMA before they are published in a user address-space root. VMA policy and page-table state MUST remain consistent for user code, data, heap, anonymous mappings, and stack pages.

#### Scenario: map user page checks VMA first

- **WHEN** kernel code maps a user page for exec, `brk`, anonymous mapping, user stack, or stack growth
- **THEN** BigOS MUST confirm the target virtual page is covered by a VMA with compatible permissions and ownership
- **AND** the resulting PTE user/writable/NX attributes MUST not grant access wider than the VMA permits

#### Scenario: mapping without VMA is rejected

- **WHEN** a user page mapping request targets a user low-half address that is not covered by a compatible VMA
- **THEN** BigOS MUST reject the mapping or enter a deterministic failure path before publishing a present PTE
- **AND** any physical page or intermediate page-table page allocated for the failed operation MUST be rolled back or left only in a documented fatal state

### Requirement: VMA 与页表范围校验职责分离

BigOS SHALL treat VMA lookup as the source of user virtual-memory policy and page-table probing as the source of currently materialized translation state. User range validation MUST consult both layers when kernel code will access user memory.

#### Scenario: VMA allows range but page missing

- **WHEN** a user range is covered by a compatible VMA but one or more pages are not currently present
- **THEN** BigOS MUST only recover if the range is an explicitly supported stack-growth fault or later documented demand-paging case
- **AND** otherwise it MUST return a deterministic error or terminate the current user process through the user fault path

#### Scenario: page present but VMA disallows access

- **WHEN** a page-table entry is present but the containing VMA is absent or lacks the requested read, write, or execute permission
- **THEN** BigOS MUST treat the user access as invalid
- **AND** it MUST NOT rely on the present PTE alone to authorize syscall user-buffer access

### Requirement: 用户地址空间页表准备的验证可复现

BigOS SHALL use source-level checks and default-off emulator smoke to validate page attribute primitives, user root derivation semantics, and the boundary between passive derivation and explicit runtime activation.

#### Scenario: 源码级检查覆盖属性与派生不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover: primitive accepts explicit attributes, kernel default attributes are equivalent to supervisor `present+writable`, user mappings set user bit, user data pages are NX / code pages are non-NX, and derived roots copy the high half while clearing the low half
- **AND** source-level checks MUST confirm the derivation helper itself does not write CR3 or enter ring3

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest available `xmake` / cross-toolchain build, relevant `uv run pytest` source-level checks, and `openspec validate prepare-user-address-space-vmem --strict` or the current change's strict validation command when this requirement is modified
- **AND** if Bochs runtime smoke cannot observe markers due to emulator, ROM, serial oracle, image lock, or interaction limits, validation MUST record the missing dependency and remaining bootability risk

### Requirement: 用户地址空间遵守 runtime layout

BigOS SHALL require every user low-half mapping in a committed process address-space root to be authorized by both the process VMA collection and the committed runtime VM layout. Page-table helpers MUST NOT publish user PTEs that widen permissions beyond the VMA or place mappings inside runtime-reserved gaps, kernel higher-half ranges, direct map, KVMEM, or recursive self-mapping ranges.

#### Scenario: user mapping checks layout and VMA

- **WHEN** kernel code maps a user page for ELF segments, heap, restricted anonymous mapping, stack growth, argument setup, or future runtime metadata
- **THEN** BigOS MUST confirm the target page is covered by a compatible VMA and by an allowed region of the committed runtime layout before publishing a present PTE
- **AND** the PTE user, writable, and no-execute attributes MUST not grant access wider than the layout and VMA permit

#### Scenario: reserved gap mapping is rejected

- **WHEN** a user mapping request targets an unmapped runtime-reserved gap or an address outside the supported runtime layout
- **THEN** BigOS MUST reject the mapping or enter a deterministic failure path before publishing a present PTE
- **AND** any physical page or intermediate page-table page allocated by the failed operation MUST be released or left only in a documented fatal state

### Requirement: address-space teardown respects layout ownership

BigOS SHALL use runtime layout ownership and VMA materialization accounting to decide which user pages and low-half page-table pages can be reclaimed. Borrowed kernel high-half entries copied into a user root MUST remain non-owned by the process address space.

#### Scenario: teardown frees only owned low-half state

- **WHEN** a process image exits, faults, execs a replacement image, or is reaped
- **THEN** BigOS MUST reclaim only pages, mappings, and dynamically owned low-half page-table pages that belong to the process runtime layout and VMA collection
- **AND** it MUST NOT reclaim page-table pages reachable only through borrowed high-half kernel PML4 entries

#### Scenario: partial image preparation rolls back address-space state

- **WHEN** image preparation fails after allocating a user root, low-half page-table page, or mapped user page but before commit
- **THEN** BigOS MUST release the uncommitted address-space state without exposing it to the old or new user image
- **AND** rollback MUST preserve the currently active CR3 and kernel diagnostic path
