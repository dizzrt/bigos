## MODIFIED Requirements

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
