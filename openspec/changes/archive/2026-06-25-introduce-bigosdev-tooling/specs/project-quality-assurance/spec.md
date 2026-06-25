## MODIFIED Requirements

### Requirement: Emulator smoke 优先级必须按场景声明

BigOS 的项目级 Agent 指南 SHALL 为 QEMU 与 Bochs 的 smoke/debug 使用场景定义明确优先级，避免将任一 emulator 描述为所有调试场景的唯一默认选择。指南 SHALL 使用当前受支持的 xmake run target、display 参数形态和 `tools.bigosdev` helper 路径，不得继续把 `bochs-sdl2`、`tools/boot_debug.py` 或 `tools/install.py` 描述为稳定入口。

#### Scenario: Agent 执行自动化 smoke 或串口 marker 验证

- **WHEN** Agent 需要执行自动化 smoke test、串口 marker 检查或 CI-like 本地验证
- **THEN** 项目级指南 MUST 要求优先使用 `xmake run qemu -- --display none` 对应的 QEMU headless display helper 路径，或直接使用 `uv run python -m tools.bigosdev run --emulator qemu --display none` 等价参数
- **AND** 指南 MUST 要求在 QEMU 不可用时显式记录缺失依赖和剩余风险

#### Scenario: Agent 执行快速本地启动验证

- **WHEN** Agent 需要执行普通快速本地 boot validation 且不需要 Bochs 交互调试
- **THEN** 项目级指南 MUST 要求优先使用 `xmake run qemu` 的图形 display 模式或等价 QEMU helper 路径

#### Scenario: Agent 排查早期启动或硬件行为差异

- **WHEN** Agent 修改或排查 boot、linker、memory initialization、IRQ、timer、syscall、ATA PIO、port IO 或低层 driver 行为
- **THEN** 项目级指南 MUST 要求在环境支持时使用 Bochs 复核或进行 Bochs/QEMU 双 emulator 交叉验证
- **AND** 指南 MUST 保留 `xmake run bochs`、`xmake run bochs -- --display sdl2` 和 `xmake run bochs -- --display none` 作为受支持的 Bochs 调试入口

#### Scenario: Emulator 验证不可用

- **WHEN** QEMU、Bochs、cross-binutils、display/ROM 依赖或本地 emulator 配置不可用
- **THEN** 项目级指南 MUST 要求 Agent 明确记录缺失工具、无法执行的验证步骤、已执行的替代检查和剩余风险

## ADDED Requirements

### Requirement: Python tooling changes must validate package entry points
OpenSpec changes that modify `tools/bigosdev/**` SHALL include Python package entry, parser, static analysis, and targeted unit validation tasks.

#### Scenario: Change modifies bigosdev Python modules
- **WHEN** a change modifies `tools/bigosdev/**`
- **THEN** its tasks MUST include `uv run python -m tools.bigosdev --help` or an equivalent package-entry parser check
- **AND** its tasks MUST include targeted Python tests, `uv run ruff check`, `uv run ruff format --check`, and `uv run pyright` when those tools are available
