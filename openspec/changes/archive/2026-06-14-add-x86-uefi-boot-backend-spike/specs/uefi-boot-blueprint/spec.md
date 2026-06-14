## ADDED Requirements

### Requirement: UEFI 蓝图进入可运行 spike 阶段

BigOS SHALL distinguish the previously documented UEFI planning blueprint from the new runnable x86_64 UEFI boot backend spike.

#### Scenario: 蓝图约束被落实为 spike

- **WHEN** 本 change 被应用
- **THEN** UEFI 蓝图 MUST 将 `BOOTX64.EFI`、ESP/FAT 镜像和 QEMU + OVMF 调试入口从后续规划推进为本 change 的可运行 spike 目标
- **AND** 它 MUST 继续标注 Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services 和第二 ISA 为非当前目标

#### Scenario: UEFI spike 不等于 runtime parity

- **WHEN** 文档或验证记录描述 UEFI backend 状态
- **THEN** 它 MUST 将该 backend 描述为 spike 或试探性 backend，直到明确达到与 Legacy BIOS backend 的 runtime parity
- **AND** 它 MUST 明确当前 Legacy BIOS/MBR/exFAT backend 仍是保留的可运行基线

### Requirement: UEFI 后续路线保持分层

BigOS SHALL keep UEFI boot backend work separated from unrelated runtime expansion tracks.

#### Scenario: 后续 UEFI 正式化被分层

- **WHEN** 后续 change 继续推进 UEFI backend
- **THEN** 它 MUST 单独说明是否覆盖 runtime parity、GOP framebuffer、ACPI table handoff、UEFI Runtime Services、Secure Boot 或持久 NVRAM 语义
- **AND** 它 MUST NOT 将这些事项隐式并入基础 loader spike

#### Scenario: 第二 ISA 仍然独立

- **WHEN** 项目讨论 AArch64、RISC-V 或其它 ISA backend
- **THEN** 该工作 MUST 作为独立 ISA spike 处理
- **AND** 它 MUST NOT 被本 x86_64 UEFI backend spike 视为已完成或隐式支持

### Requirement: UEFI spike 文档同步

BigOS SHALL update the bilingual architecture documentation when the runnable x86_64 UEFI backend spike is implemented.

#### Scenario: 英文架构文档记录 UEFI spike

- **WHEN** 本 change 的实现完成
- **THEN** `docs/en` 中对应架构文档 MUST describe the UEFI backend as an x86_64 boot backend spike
- **AND** it MUST document the OVMF/QEMU, LLVM/LLD, mtools, BootInfo storage metadata, BootInfo loader metadata, and Legacy/UEFI debug-entry boundaries

#### Scenario: 中文架构文档同步

- **WHEN** 英文架构文档被更新
- **THEN** `docs/zh` 中对应相对路径文档 MUST 同步说明相同的 UEFI spike 边界、工具链假设和 metadata section 语义
- **AND** 它 MUST NOT 将本 change 描述为第二 ISA、默认 UEFI runtime parity backend、Secure Boot、GOP framebuffer、ACPI table handoff 或 UEFI Runtime Services 支持
