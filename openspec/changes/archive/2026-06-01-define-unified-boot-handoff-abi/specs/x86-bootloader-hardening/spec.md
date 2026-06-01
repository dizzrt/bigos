## ADDED Requirements

### Requirement: Legacy BIOS backend supports unified handoff migration

现有 x86 Legacy BIOS bootloader SHALL 在保持当前可启动路径兼容的同时，作为统一 boot handoff ABI 的 v2 producer。

#### Scenario: Legacy BIOS 继续生成 v1 BootInfo

- **WHEN** Legacy BIOS bootloader 完成 kernel ELF64 加载
- **THEN** documented handoff address 处 MUST 继续存在可由现有 v1 ABI 校验的 `BootInfo`
- **AND** 现有 E820 metadata、boot drive、kernel load/entry metadata 和 kernel size metadata MUST 保持兼容

#### Scenario: Legacy BIOS 生成 v2 handoff blob

- **WHEN** Legacy BIOS bootloader 完成 kernel ELF64 加载并准备进入 kernel
- **THEN** 它 MUST 生成完整 v2 handoff blob，包含 boot protocol/core section 和由 BIOS E820 规范化而来的 memory map section
- **AND** v2 blob 的实际存放地址 MUST 避免覆盖现有 E820 buffer、legacy metadata aliases、v1 `BootInfo`、boot-stage page table 区域和 kernel load base

#### Scenario: Legacy BIOS 设置 register handoff

- **WHEN** Legacy BIOS bootloader 跳转到已校验的 kernel ELF entry
- **THEN** 它 MUST 设置 x86_64 第一个参数寄存器为 v2 `BootInfoHeader*`
- **AND** 它 MUST 保留 fixed low-address `BootInfo` 作为迁移期 fallback

#### Scenario: Legacy BIOS 不新增长期固件耦合

- **WHEN** Legacy BIOS backend 为统一 memory map consumer 提供输入
- **THEN** 它 MUST 通过 v2 memory map section 暴露规范化后的 E820 数据
- **AND** kernel memory、driver 或 IRQ 子系统 MUST NOT 新增对 BIOS interrupt、raw E820 fixed address 或 BIOS-only boot metadata 的长期依赖

### Requirement: Legacy boot-debug semantics remain unchanged

统一 boot handoff ABI SHALL NOT 改变现有 Legacy BIOS boot-debug 入口的用户语义。

#### Scenario: make boot-debug 仍代表 Legacy BIOS

- **WHEN** 开发者运行现有 `make boot-debug` 或等价 Legacy BIOS 调试入口
- **THEN** 构建和运行路径 MUST 继续使用现有 BIOS/MBR/exFAT/`boot.bin`/Bochs 语义
- **AND** 该入口 MUST NOT 隐式切换到 `BOOTX64.EFI`、ESP image 或 QEMU/OVMF UEFI 路径

#### Scenario: ABI 迁移不移动既有低地址布局

- **WHEN** register-passed v2 `BootInfoHeader*` 或 unified memory region view 被实现
- **THEN** 它 MUST NOT 移动现有 E820 buffer、legacy metadata aliases、v1 `BootInfo` address、boot-stage page table 区域或 kernel load base
- **AND** 如需新增固定地址或保留区，change MUST 更新地址布局文档并说明与未来 UEFI backend 的兼容关系
