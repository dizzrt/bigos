## ADDED Requirements

### Requirement: 页表映射使用显式页属性

BigOS SHALL 提供一个显式的页属性描述与 map/unmap primitive，用统一的属性 bit
（present、writable、user、no-execute、global）驱动页表条目写入，而不是在多处硬编码裸 PTE 常量。

#### Scenario: map primitive 接受显式属性

- **WHEN** 内核代码在非中断上下文调用页表 map primitive 建立一个虚拟页到物理页的映射
- **THEN** primitive MUST 接受一个显式页属性入参，并据此设置 PTE 的 present、writable、user、
  no-execute bit
- **AND** primitive MUST 在缺少中间页表级时按现有方式分配页表页，并在写入条目时屏蔽 same-CPU
  maskable IRQ 交织

#### Scenario: unmap primitive 清除条目并刷新 TLB

- **WHEN** 内核代码对一个已映射虚拟页调用 unmap primitive
- **THEN** primitive MUST 清除对应 PTE
- **AND** primitive MUST 对该虚拟地址执行 TLB 失效（如 `invlpg`），保持与现有内核 unmap 路径一致的行为

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

BigOS SHALL 提供一个最小的用户地址空间页表根派生能力，使内核高半区在派生根中共享，用户低半区独立。
本阶段 SHALL NOT 切换 CR3 或进入 ring3。

#### Scenario: 派生根复制内核高半区条目

- **WHEN** 内核代码基于当前内核 PML4 派生一个新的用户地址空间页表根
- **THEN** 派生根 MUST 复制内核 PML4 高半区顶层条目（覆盖 kernel higher-half、self-mapping、
  direct map、KVMEM 所在区域）
- **AND** 派生根的低半区（用户区）顶层条目 MUST 被清零以保证用户地址空间相互独立

#### Scenario: 派生不切换地址空间

- **WHEN** 用户地址空间页表根被派生
- **THEN** BigOS MUST NOT 在本阶段写入 CR3 切换到该根
- **AND** BigOS MUST NOT 进入 ring3、加载用户代码或实现 demand paging
- **AND** `#PF` handler MUST 保持诊断-only，不做恢复

### Requirement: 用户地址空间页表准备的验证可复现

BigOS SHALL 用源码级检查与默认关闭的 emulator smoke 验证页属性 primitive 与用户根派生语义。

#### Scenario: 源码级检查覆盖属性与派生不变量

- **WHEN** 本 change 实现完成
- **THEN** 源码级检查 MUST 覆盖：primitive 接受显式属性、内核默认属性等价 supervisor `present+writable`、
  用户映射置 user bit、用户数据页 NX / 代码页非 NX，以及派生根复制高半区且清零低半区
- **AND** 源码级检查 MUST 确认本阶段不写 CR3、不进入 ring3

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** 实现完成
- **THEN** 验证 MUST 记录最窄可用的 `xmake` / cross-toolchain 构建、相关 `uv run pytest` 源码级检查，
  以及 `openspec validate prepare-user-address-space-vmem --strict`
- **AND** 若 Bochs runtime smoke 因 emulator、ROM、serial oracle、image lock 或交互限制无法观测 marker，
  验证 MUST 记录缺失依赖与剩余 bootability 风险
