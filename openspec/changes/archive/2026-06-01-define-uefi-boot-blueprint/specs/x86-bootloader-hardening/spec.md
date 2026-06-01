## ADDED Requirements

### Requirement: Legacy BIOS 路径保持为统一 handoff producer

现有 x86 BIOS/MBR/exFAT 启动路径 SHALL 在 UEFI 蓝图阶段继续保持可用，并作为统一 kernel handoff 的一个 producer 进行规划。

#### Scenario: BIOS boot hardening 不被 UEFI 蓝图破坏

- **WHEN** UEFI 蓝图 change 被应用
- **THEN** 现有 BIOS/MBR/exFAT 启动路径的 boot artifact、地址布局、ELF64 加载和 `BootInfo` 生成语义 MUST 保持不变

#### Scenario: BIOS 路径参与 handoff 演进

- **WHEN** 后续 change 扩展 `BootInfo` 或统一 memory map 契约
- **THEN** BIOS backend MUST 明确如何填充新契约或如何保留文档化 fallback
- **AND** 它 MUST NOT 静默生成与 kernel consumer 不匹配的 handoff 数据

#### Scenario: BIOS 路径适配寄存器 handoff

- **WHEN** 后续 change 将 kernel entry ABI 改为寄存器传递 `BootInfo*`
- **THEN** BIOS backend MUST 明确如何设置该寄存器并保留必要的 Legacy 低地址 fallback
- **AND** 它 MUST NOT 只依赖固定低地址作为长期 handoff ABI

### Requirement: 固件差异不得扩散到 kernel 子系统

x86 bootloader hardening SHALL 把 BIOS 固件专用输入限制在 boot backend 内，并通过统一 handoff 将规范化结果交给 kernel。

#### Scenario: BIOS 固件数据被规范化

- **WHEN** BIOS backend 收集 E820、boot drive、磁盘布局或视频相关启动数据
- **THEN** 它 MUST 通过文档化 handoff 字段暴露 kernel 需要的数据
- **AND** kernel 子系统 MUST NOT 新增对 BIOS interrupt 调用路径的依赖

#### Scenario: BIOS 固定地址新增依赖受控

- **WHEN** 后续 BIOS boot change 需要新增固定低地址、page-table reservation 或 handoff alias
- **THEN** 该 change MUST 更新地址布局文档和构建期校验策略
- **AND** 它 MUST 说明该新增依赖与未来 UEFI backend 的兼容关系
