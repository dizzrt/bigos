## ADDED Requirements

### Requirement: Emulator smoke 优先级必须按场景声明

BigOS 的项目级 Agent 指南 SHALL 为 QEMU 与 Bochs 的 smoke/debug 使用场景定义明确优先级，避免将任一 emulator 描述为所有调试场景的唯一默认选择。

#### Scenario: Agent 执行自动化 smoke 或串口 marker 验证

- **WHEN** Agent 需要执行自动化 smoke test、串口 marker 检查或 CI-like 本地验证
- **THEN** 项目级指南 MUST 要求优先使用 `xmake run qemu` 对应的 QEMU headless display helper 路径，例如 `--display none` 或等价参数
- **AND** 指南 MUST 要求在 QEMU 不可用时显式记录缺失依赖和剩余风险

#### Scenario: Agent 执行快速本地启动验证

- **WHEN** Agent 需要执行普通快速本地 boot validation 且不需要 Bochs 交互调试
- **THEN** 项目级指南 MUST 要求优先使用 `xmake run qemu` 的图形 display 模式或等价 QEMU helper 路径

#### Scenario: Agent 排查早期启动或硬件行为差异

- **WHEN** Agent 修改或排查 boot、linker、memory initialization、IRQ、timer、syscall、ATA PIO、port IO 或低层 driver 行为
- **THEN** 项目级指南 MUST 要求在环境支持时使用 Bochs 复核或进行 Bochs/QEMU 双 emulator 交叉验证
- **AND** 指南 MUST 保留 `xmake run bochs-sdl2` 和 `xmake run bochs` 作为受支持的 Bochs 调试入口

#### Scenario: Emulator 验证不可用

- **WHEN** QEMU、Bochs、cross-binutils、display/ROM 依赖或本地 emulator 配置不可用
- **THEN** 项目级指南 MUST 要求 Agent 明确记录缺失工具、无法执行的验证步骤、已执行的替代检查和剩余风险

## MODIFIED Requirements

### Requirement: 不可用验证必须显式记录

如果必需验证步骤因本地工具、emulator 配置、disk image paths、compile database 生成或项目设置不可用而无法执行，该 change 必须（MUST）在 ready for review 前记录跳过的验证、无法运行的原因和剩余风险。

#### Scenario: 工具不可用

- **WHEN** 必需验证步骤因工具缺失或配置错误而无法运行
- **THEN** 验证记录必须（MUST）说明缺失或配置错误的工具，并描述剩余风险

#### Scenario: Emulator smoke test 不可用

- **WHEN** 必需 emulator smoke test 因 QEMU、Bochs、disk image 配置、display/ROM 配置或本地 emulator 环境不可用而无法运行
- **THEN** 验证记录必须（MUST）说明缺失的 emulator 设置和已执行的替代验证
