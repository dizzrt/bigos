## 1. Matrix Definition

- [x] 1.1 盘点现有 runtime smoke 开关、编译边界、预期 COM1/VGA marker、case-specific timeout 和输出日志路径。
- [x] 1.2 定义runtime smoke validation matrix runtime smoke matrix，至少包含 memory self-test、timer IRQ、scheduler、syscall、read-only filesystem、first user program、filesystem-backed user ELF 的窄 case。
- [x] 1.3 为 `user_program_smoke` 和 `user_elf_smoke` case 明确记录 `kernel/core/proc/**` 编译边界和非默认 boot 边界。
- [x] 1.4 确认矩阵定义不会改变现有 smoke 默认关闭语义、marker 字符串、boot image layout 或内核运行时 ABI。

## 2. Runner And Artifact

- [x] 2.1 扩展 `tools/boot_debug.py` 子命令作为 matrix runner，用 `uv run python ...` 执行 matrix case，并通过 `xmake f` 显式配置每个 case 的 smoke 开关。
- [x] 2.2 让 runner 复用现有 xmake-backed Legacy BIOS/MBR/exFAT image 和 QEMU headless marker 路径，避免新增 bootloader、storage driver 或 UEFI 依赖。
- [x] 2.3 为每个 case 支持独立 timeout，给 filesystem 和 user ELF 等慢路径配置更长默认值，并记录 timeout/exit status。
- [x] 2.4 为每个 case 记录 expected marker、observed marker、serial log path、passed/failed/skipped/blocked 状态和失败阶段。
- [x] 2.5 生成 Markdown-first 结构化 validation artifact 到 `build/test/` 或用户显式指定路径，包含 JSON schema 兼容字段、工具可用性、case 结果、跳过原因、替代检查和剩余风险。
- [x] 2.6 对缺少 `uv`、`xmake`、`x86_64-elf-*`、QEMU、Bochs、ROM/display 配置的场景输出明确 skipped/blocked 记录，不得标记为 passed。

## 3. Documentation

- [x] 3.1 更新 `docs/en` 中的验证或运行文档，说明 runtime smoke matrix、`tools/boot_debug.py` 子命令、QEMU headless 首选自动化路径、单 case 手工执行方式、case-specific timeout 和 validation artifact 字段。
- [x] 3.2 同步更新 `docs/zh` 中匹配相对路径的中文文档，保持与 `docs/en` 内容结构一致。
- [x] 3.3 记录 boot、IRQ、timer、ATA PIO、port IO 相关变更应在可用时使用 Bochs 或 QEMU+Bochs 交叉验证，并说明不可用时的记录格式。
- [x] 3.4 明确runtime smoke validation matrix 非目标：不新增 OS runtime feature、不引入 CI 平台、不实现 UEFI、不改变 smoke marker ABI。

## 4. Validation

- [x] 4.1 为新增或修改的 Python helper 添加或更新 `uv run pytest` 覆盖矩阵解析、命令生成、marker 结果解析、跳过原因和 artifact 输出。
- [x] 4.2 对 Python 变更运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，记录阻塞原因和剩余风险。
- [x] 4.3 运行最窄可行的 `xmake` 构建，确认 helper 使用的 smoke 配置仍可由权威 GCC 交叉工具链构建；若交叉工具链不可用，记录缺失工具和剩余风险。
- [x] 4.4 在 QEMU 可用时运行至少一个 headless marker smoke，优先验证 `mm_self_test` 或等价最小 case，并保存 generated validation artifact。
- [x] 4.5 在环境支持时对涉及 boot/IRQ/timer/ATA PIO/port IO 的 case 执行 Bochs 或 QEMU+Bochs 交叉验证；不可用时记录跳过原因和替代检查。
- [x] 4.6 验证 OpenSpec artifacts 状态，确认 `proposal.md`、`design.md`、`specs/runtime-smoke-validation/spec.md` 和 `tasks.md` 均满足 apply 前要求。
