## ADDED Requirements

### Requirement: Legacy boot-debug 命令语义保持稳定

BigOS SHALL 保持 `make boot-debug` 作为 Legacy BIOS/MBR/exFAT 本地启动调试入口，UEFI 蓝图不得改变该命令的固件协议语义。

#### Scenario: boot-debug 继续启动 BIOS 路径

- **WHEN** 开发者运行 `make boot-debug`
- **THEN** 该命令 MUST 继续构建并启动现有 MBR、DBR、extended DBR、`boot.bin` 和 root `kernel` 组成的 Legacy BIOS 调试路径
- **AND** 它 MUST NOT 隐式切换为 UEFI loader、ESP 镜像或 OVMF 配置

#### Scenario: 文档描述 boot-debug 范围

- **WHEN** 文档描述 `make boot-debug`
- **THEN** 文档 MUST 明确该命令服务 Legacy BIOS 调试路径
- **AND** 文档 MUST 将 UEFI 调试入口标记为未来独立命令规划，而不是当前命令的替代语义

### Requirement: UEFI 调试入口独立规划

未来 UEFI 本地启动调试入口 SHALL 使用独立命名和独立镜像/模拟器配置规划。

#### Scenario: 规划 UEFI 调试命令

- **WHEN** 项目级路线图描述未来 UEFI 启动调试
- **THEN** 它 MUST 使用独立入口名称，例如 `make uefi-boot-debug` 或等价 project-level wrapper
- **AND** 它 MUST 明确该入口将使用 UEFI loader、ESP/FAT 镜像和 QEMU + OVMF 作为首选 UEFI 固件配置

#### Scenario: Legacy 与 UEFI 模拟器选择明确

- **WHEN** 文档描述本地启动调试矩阵
- **THEN** 它 MUST 明确 Legacy BIOS 路径继续使用 Bochs
- **AND** 它 MUST 明确 UEFI smoke test 首选 QEMU + OVMF，Bochs UEFI 仅作为可选验证路径

#### Scenario: 两类调试产物隔离

- **WHEN** 后续 change 实现 UEFI 启动调试入口
- **THEN** 它 MUST 将 UEFI 镜像、固件配置和临时产物与现有 BIOS raw image/Bochs 配置隔离
- **AND** 它 MUST NOT 覆盖 `make boot-debug` 生成的 Legacy BIOS 调试产物，除非用户显式指定同一路径
