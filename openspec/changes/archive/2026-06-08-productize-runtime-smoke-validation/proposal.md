## Why

BigOS 已经具备多条默认关闭的 runtime smoke 路径，但当前验证仍依赖开发者手动组合 `xmake f`、emulator helper 和串口 marker，难以稳定复现、记录跳过原因或作为后续 blocking、scheduler、process、VFS 等阶段的回归基线。

runtime smoke validation matrix 需要先把现有 smoke 开关和 QEMU headless marker 检查产品化为窄而明确的验证矩阵，降低后续低层 runtime 变更的回归风险。

## What Changes

- 新增 runtime smoke validation matrix，覆盖 memory、timer、scheduler、syscall、filesystem、first user program、filesystem-backed user ELF 的最小有价值组合。
- 新增结构化 validation artifact，用于记录工具可用性、执行/跳过的 smoke、串口 marker、日志路径、失败原因、替代检查和剩余风险。
- 将自动化 smoke 的首选路径定义为 QEMU headless marker 检查，并保留 boot、IRQ、timer、ATA PIO、port IO 相关变更的 Bochs 或 QEMU+Bochs 交叉验证建议。
- 保持所有 runtime smoke 开关默认关闭，继续通过 `xmake f ...=y` 显式配置，不改变 kernel boot protocol、disk image layout、interrupt ABI、syscall ABI 或用户态 smoke 边界。
- 明确非目标：不引入 CI 平台集成、不实现新 OS 功能、不扩展 preemption/SMP/process/VFS/VM、不新增 UEFI backend、不改变现有 smoke marker ABI。

## Capabilities

### New Capabilities
- `runtime-smoke-validation`: 定义 BigOS runtime smoke validation matrix 的可重复 runtime smoke 矩阵、QEMU headless marker 执行模型、结构化验证记录和跳过原因规则。

### Modified Capabilities
- 无。现有 `one-command-boot-debug`、`project-quality-assurance` 和各子系统 runtime validation 规格保持不变；本 change 只新增跨子系统验证编排能力。

## Impact

- 影响子系统：build/run tooling、`tools/boot_debug.py` 周边 helper、`xmake` smoke 配置使用方式、测试/验证脚本、文档与 OpenSpec 验证流程。
- 运行时边界：不修改 bootloader、kernel link address、BootInfo/handoff ABI、IDT/IRQ/syscall ABI、CR3/地址空间所有权或 smoke marker 字符串。
- 架构假设：x86_64 single-core Legacy BIOS/MBR/exFAT 路径，现有 higher-half ELF64 kernel，现有 COM1/VGA marker 输出。
- 工具链假设：`xmake` 和 `x86_64-elf-gcc`/`x86_64-elf-g++` 是权威构建路径；Python helper 通过 `uv run python ...` 执行。
- Emulator 假设：自动化优先 QEMU headless；涉及 boot、IRQ、timer、ATA PIO、port IO 的高风险验证在本地可用时使用 Bochs 或 QEMU+Bochs 交叉验证。
- 磁盘布局假设：沿用现有 Legacy BIOS raw image、IDE disk、MBR/exFAT partition、`/boot/boot.bin` 和 root `kernel`，`user_elf_smoke` 继续打包 `/boot/user/init.elf`。
