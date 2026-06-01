## ADDED Requirements

### Requirement: UEFI 启动蓝图

BigOS SHALL 维护一份 UEFI 启动蓝图，用于指导后续 boot、内存、显示、ACPI、磁盘和调试入口的兼容性设计。

#### Scenario: 蓝图限定当前范围

- **WHEN** 本 change 被应用
- **THEN** UEFI 蓝图 MUST 明确当前阶段只建立设计约束和项目级规划
- **AND** 它 MUST NOT 要求立即实现 `BOOTX64.EFI`、ESP 镜像生成或 UEFI emulator smoke test

#### Scenario: 后续模块查阅兼容约束

- **WHEN** 后续内存、显示、ACPI、磁盘或启动调试相关 change 被起草
- **THEN** 对应设计 MUST 考虑 UEFI 蓝图中的 handoff、memory map、framebuffer、firmware table 和调试入口约束

### Requirement: 统一 kernel handoff 原则

BigOS kernel SHALL 通过规范化后的 boot handoff 消费启动信息，而不是直接依赖 BIOS interrupt、UEFI Boot Services 或固件专用 descriptor。

#### Scenario: BIOS 与 UEFI 数据规范化

- **WHEN** 一个 boot backend 进入 kernel 前收集固件启动数据
- **THEN** 该 backend MUST 将 BIOS 或 UEFI 数据规范化为 kernel handoff 契约中定义的数据
- **AND** kernel 初始化路径 MUST NOT 直接调用 BIOS interrupt 或 UEFI Boot Services

#### Scenario: kernel 入口来源无关

- **WHEN** kernel 从 Legacy BIOS backend 或未来 UEFI backend 启动
- **THEN** `kernel()` 及其后续初始化 MUST 只依赖统一 handoff 中的字段区分启动来源和硬件描述

### Requirement: BootInfo 后续版本规划

UEFI 蓝图 SHALL 规划 `BootInfo` 的版本化演进，以支持 BIOS 与 UEFI backend 共同进入同一个 kernel handoff ABI。

#### Scenario: BootInfo 扩展字段被规划

- **WHEN** 项目规划 `BootInfo` 后续版本
- **THEN** 规划 MUST 采用 `BootInfoHeader + tagged sections` 作为长期方向
- **AND** 规划 MUST 覆盖 boot protocol、统一 memory map、framebuffer、firmware tables、loader metadata 和 ABI 兼容策略

#### Scenario: 分阶段引入 tagged sections

- **WHEN** 下一阶段设计 `BootInfo` 后续版本
- **THEN** 它 MUST 允许先定义 header 和少量固定 core 字段
- **AND** 它 MUST 将 memory map、framebuffer 和其它变长或可选数据规划为逐步迁移到 tagged sections

#### Scenario: BootInfo 兼容关系明确

- **WHEN** 后续 change 修改 `BootInfo` ABI
- **THEN** 该 change MUST 明确旧版 BIOS path 的兼容、fallback 或迁移策略
- **AND** 它 MUST 说明 magic、version、size、alignment 和字段 offset 的校验方式

#### Scenario: kernel entry 使用寄存器传递 BootInfo 指针

- **WHEN** 后续 change 定义新的 kernel entry ABI
- **THEN** ABI MUST 规定通过约定寄存器传递 `BootInfo*`
- **AND** 固定低地址 handoff MUST 仅作为 Legacy BIOS 迁移期 fallback 或调试兼容手段

### Requirement: 统一内存图规划

UEFI 蓝图 SHALL 定义内存模块面向统一 `BootMemoryRegion` 视图演进的规划，以便未来 UEFI `GetMemoryMap` 可由 loader 转换后交给 kernel。

#### Scenario: 内存区域格式被规划

- **WHEN** 规划统一内存图格式
- **THEN** 它 MUST 至少表达物理起始地址、长度、规范化类型和属性
- **AND** 它 MUST 记录 BIOS E820 type 与 UEFI memory type 映射需要单独设计和验证

#### Scenario: Runtime memory metadata 被保留

- **WHEN** 规划 UEFI `GetMemoryMap` 到统一 memory map 的转换
- **THEN** 规划 MUST 明确近期不支持调用 UEFI Runtime Services
- **AND** 规划 MUST 保留 runtime memory 类型和 attributes，不得在规范化阶段丢弃这些 metadata

#### Scenario: 内存模块避免固件耦合

- **WHEN** 后续内存模块 change 消费启动内存图
- **THEN** 它 MUST 优先面向统一 memory map consumer 设计
- **AND** 它 MUST NOT 新增对 UEFI raw descriptor 或 BIOS 魔法地址的长期依赖

### Requirement: 项目级 UEFI 路线图

UEFI 蓝图 SHALL 将当前不实现但后续阶段计划要做的事项整理为项目级路线图。

#### Scenario: 路线图覆盖未来阶段

- **WHEN** 蓝图任务被完成
- **THEN** 项目级规划 MUST 覆盖 UEFI loader spike、ESP/FAT 镜像、OVMF/QEMU 调试入口、BootInfo v2、内存模块迁移、GOP framebuffer、ACPI table handoff 和验证策略

#### Scenario: ELF64 加载规则作为共同规范

- **WHEN** 项目级规划描述未来 UEFI loader
- **THEN** 它 MUST 明确 UEFI loader 单独实现适合 UEFI 的 ELF reader
- **AND** 它 MUST 要求 BIOS 与 UEFI loader 遵循共同的 ELF64 加载规则规范

#### Scenario: UEFI smoke test 模拟器选择

- **WHEN** 项目级规划描述未来 UEFI smoke test
- **THEN** 它 MUST 将 QEMU + OVMF 作为首选验证路径
- **AND** 它 MUST 将 Bochs UEFI 标记为可选验证路径，同时保留 Legacy BIOS 继续使用 Bochs

#### Scenario: 路线图标注非当前阶段事项

- **WHEN** 某项工作被列为后续阶段计划
- **THEN** 它 MUST 明确标注为规划项或后续 change 候选项
- **AND** 它 MUST NOT 被误认为本 change 的运行时代码实现任务
