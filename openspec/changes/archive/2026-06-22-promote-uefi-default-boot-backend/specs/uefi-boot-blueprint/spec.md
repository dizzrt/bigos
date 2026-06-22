## MODIFIED Requirements

### Requirement: UEFI 蓝图进入可运行 spike 阶段

BigOS SHALL distinguish the previously documented UEFI planning blueprint, the already implemented runnable x86_64 UEFI boot backend spike, and the promoted default runnable UEFI backend state.

#### Scenario: 蓝图约束被落实为 spike

- **WHEN** runnable x86_64 UEFI backend artifacts exist
- **THEN** UEFI 蓝图 MUST 将 `BOOTX64.EFI`、ESP/FAT 镜像和 QEMU + OVMF 调试入口记录为已经从后续规划推进到可运行 spike 的能力
- **AND** 它 MUST 继续标注 Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services 和第二 ISA 为独立后续目标

#### Scenario: UEFI spike 晋升为默认 runnable backend

- **WHEN** 本 change 被应用
- **THEN** UEFI 蓝图 MUST 将 x86_64 UEFI backend 描述为默认可运行 boot backend，并说明其 runtime parity 边界是当前 bounded userland baseline
- **AND** 它 MUST 明确 Legacy BIOS/MBR/exFAT backend 仍作为显式可选 backend 保留，不因 UEFI 默认化而删除

#### Scenario: UEFI 默认化不等于后续 firmware parity

- **WHEN** 文档或验证记录描述 UEFI backend 状态
- **THEN** 它 MUST NOT 继续把 UEFI backend 描述为 non-parity spike
- **AND** 它 MUST NOT 将默认 UEFI backend 误描述为 Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services、NVRAM 持久语义或第二 ISA 支持

### Requirement: UEFI spike 文档同步

BigOS SHALL update the bilingual architecture documentation when the runnable x86_64 UEFI backend is promoted to the default bounded userland boot backend.

#### Scenario: 英文架构文档记录 UEFI 默认 backend

- **WHEN** 本 change 的实现完成
- **THEN** `docs/en` 中对应架构文档 MUST describe the UEFI backend as the default x86_64 runnable boot backend within the bounded userland baseline
- **AND** it MUST document the OVMF/QEMU, LLVM/LLD, mtools, BootInfo storage metadata, BootInfo loader metadata, Legacy/UEFI debug-entry boundaries, and remaining non-goals

#### Scenario: 中文架构文档同步

- **WHEN** 英文架构文档被更新
- **THEN** `docs/zh` 中对应相对路径文档 MUST 同步说明相同的 UEFI 默认 backend 边界、工具链假设、metadata section 语义和 Legacy BIOS 保留语义
- **AND** 它 MUST NOT 将本 change 描述为第二 ISA、Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services、完整 POSIX 或完整存储/设备支持
