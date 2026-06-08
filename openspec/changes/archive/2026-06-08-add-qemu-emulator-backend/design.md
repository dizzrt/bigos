## Context

BigOS 现有本地启动调试路径由 `xmake run bochs` 和 `xmake run bochs-sdl2` 触发，最终复用 `tools/boot_debug.py` 完成 artifact 检查、raw image 生成、exFAT 布局写入、Bochs 配置生成、Bochs 启动和串口 marker 等流程。该路径对 Legacy BIOS/MBR/exFAT 启动调试足够稳定，但所有 emulator 相关逻辑都绑定 Bochs，导致 headless 自动化和快速 smoke 缺少更合适的 QEMU 入口。

本 change 只在开发工具层接入 QEMU，不修改内核、bootloader、linker、BootInfo/handoff、磁盘镜像布局或驱动行为。QEMU backend 必须继续使用当前 raw image，并通过 IDE disk 暴露给现有 ATA PIO 路径。

现有流程和目标流程可以抽象为：

```text
xmake run <target>
        |
        v
  boot_debug.py run
        |
        +--> build_current_artifacts() 或 --skip-build 复用 xmake 构建结果
        +--> create_image(): MBR + exFAT DBR + /boot/boot.bin + root kernel
        +--> validate_image()
        |
        +--> Bochs backend: render_bochsrc() + bochs -f ...
        |
        +--> QEMU backend: qemu-system-x86_64 -drive if=ide ... -serial ...
```

## Goals / Non-Goals

**Goals:**

- 在 `tools/boot_debug.py` 中引入 emulator 选择，支持 `bochs`、`bochs-sdl2`、`qemu`、`qemu-gdb`，并通过 `--display` 或等价参数控制 QEMU 图形/无图形模式。
- 新增 `xmake run qemu`、`xmake run qemu-gdb` 或等价命名 target，作为与 Bochs 对等的本地启动入口。
- 复用现有 raw image builder、exFAT 布局、串口日志和 `--expect-serial-marker` 机制。
- QEMU backend 使用 Legacy BIOS + IDE disk，使现有 ATA PIO driver 和 bootloader 路径保持不变。
- 更新文档和 `AGENTS.md`，明确 Agent 在自动化 smoke/串口 marker/CI-like 验证中优先使用 `qemu --display none` 或等价 helper 参数，在快速本地启动验证中优先使用 `qemu --display graphical` 或默认图形模式，在早期 boot 和硬件行为差异排查中使用或交叉验证 Bochs。
- 对缺失 QEMU、Bochs、cross-toolchain 或 emulator 运行环境的场景提供明确失败信息。

**Non-Goals:**

- 不实现 UEFI loader、ESP/FAT 镜像、OVMF 配置或 `qemu-ovmf`/`qemu-uefi` 启动入口。
- 不修改 Legacy BIOS boot sector、DBR、extended DBR、`boot.bin`、kernel ELF 加载规则或 higher-half kernel 地址。
- 不切换磁盘设备模型到 virtio、AHCI/SATA、NVMe 或其它当前 kernel 不支持的路径。
- 不将 QEMU 设为完全替代 Bochs 的唯一权威 emulator。
- 不引入 CI 平台配置；本 change 只提供可被 CI 使用的本地命令和验证约定。

## Decisions

### Decision: 在 Python helper 中抽象 emulator backend

`tools/boot_debug.py` 应增加显式 emulator/backend 参数，而不是在 `xmake.lua` 中硬编码完整 QEMU 命令。这样 raw image 构建、artifact 校验、串口 marker 轮询、timeout 和失败报告可以在一个入口内复用。

备选方案：

- 在 `xmake.lua` 中直接新增 `qemu-system-x86_64` 命令。实现简单，但会复制 image path、serial log、marker、timeout 和 preflight 逻辑，后续维护成本高。
- 新增独立 `tools/qemu_debug.py`。隔离性强，但会重复 boot image 构建和验证逻辑。

选择 Python helper 抽象 backend，因为 QEMU 与 Bochs 的差异集中在 launch/config 阶段，前置 artifact 与 image 构建完全相同。

### Decision: QEMU 使用 IDE disk 和 Legacy BIOS

QEMU backend 必须使用类似 `-drive file=<image>,format=raw,if=ide` 的磁盘暴露方式，并通过默认 SeaBIOS/Legacy BIOS 启动当前 MBR。这样现有 ATA PIO driver、MBR/DBR/boot.bin 路径和 exFAT 布局不需要变化。

备选方案：

- 使用 virtio block。性能更好，但 BigOS 当前没有 virtio driver，不符合当前 boot/runtime 能力。
- 使用 AHCI/SATA 或 NVMe。更接近现代硬件，但会扩大到新存储驱动范围。
- 使用 OVMF。适合未来 UEFI 路线，但与当前 Legacy BIOS/MBR 目标不同。

### Decision: 只保留 QEMU 和 QEMU GDB 入口，显示模式由参数控制

建议只提供两个 QEMU xmake target：

- `xmake run qemu`: 普通 QEMU 启动入口，默认使用图形 display，写入串口日志；自动化 smoke 通过 Python helper 的 `--display none` 或等价参数切换为无图形模式。
- `xmake run qemu-gdb`: 使用 `-S -s` 或等价参数暂停启动并开放 GDB stub，默认使用图形 display，便于同时观察 VGA 输出和 GDB 调试；需要远程/headless GDB 时通过 `--display none` 或等价参数切换。

备选方案是额外提供 `qemu-headless` 和 `qemu-gdb-headless` target。该方案入口更直观，但会增加 target 数量和文档维护面；QEMU 自身已经有 display 参数，使用 helper 参数表达图形/无图形更清晰。

### Decision: 提供 QEMU escape hatch 但不纳入稳定测试契约

Python helper 建议提供类似 `--bochs-extra` 的 `--qemu-extra` 参数，用于本地临时实验、调试 host-specific QEMU 参数或验证特殊硬件模型。该参数只作为 escape hatch，不作为文档化 smoke、CI-like 验证或 OpenSpec 验收的稳定契约；稳定验证必须依赖明确建模的参数，例如 emulator、display、serial log、GDB stub、timeout 和 disk image path。

### Decision: 串口 marker 轮询复用语义但适配 QEMU 停止行为

QEMU smoke 与 Bochs smoke 一样应监控 serial log 中的 marker，观察到 marker 后终止 emulator 进程组并报告成功。QEMU 进程退出码、窗口关闭和 `-no-reboot` 行为与 Bochs 不同，因此停止/失败判断应单独实现，不复用 Bochs 的 `User requested shutdown.` 逻辑。

### Decision: AGENTS 规则采用“分场景优先级”

`AGENTS.md` 不应写成“所有调试都优先 QEMU”。推荐规则是：

- 自动化 smoke、串口 marker、CI-like 验证优先 `qemu` 的无图形 display 模式。
- 快速本地启动验证优先 `qemu` 的图形 display 模式或默认模式。
- 早期 boot、BIOS、实模式/保护模式/长模式切换、ATA PIO、中断或硬件行为差异排查使用 Bochs 或双 emulator 交叉验证。
- 工具缺失时必须显式记录，不得声称完成 runtime validation。

该规则保留 Bochs 对严格 x86 调试的价值，同时让日常自动化获得 QEMU 的速度和 headless 便利。

## Risks / Trade-offs

- QEMU 对某些早期 x86/BIOS 行为可能比 Bochs 宽松 -> 高风险 boot、linker、memory init、IRQ、timer、syscall 或 driver change 在环境允许时用 Bochs 交叉验证。
- QEMU 磁盘模型选择错误会绕开现有 ATA PIO 假设 -> 固定使用 IDE/Legacy BIOS，并在 spec 和测试中覆盖命令参数。
- QEMU 进程停止逻辑与 Bochs 不同，可能造成 smoke 超时后残留进程 -> 为 QEMU backend 单独实现进程组终止和 timeout 清理。
- 本地环境可能缺少 `qemu-system-x86_64` -> preflight 只在选择 QEMU backend 时要求 QEMU，并输出明确缺失工具。
- 新增 emulator target 可能让文档产生歧义 -> README、README-zh、AGENTS 同步说明 Bochs/QEMU 的适用场景和 UEFI 非目标。

## Migration Plan

- 先扩展 `tools/boot_debug.py` 的命令行和 backend launch 层，不改变默认 `bochs` 行为。
- 再新增 xmake phony target，让 `qemu` 系列 target 复用 `--skip-build` 的现有 xmake 构建结果。
- 然后补充测试，覆盖 preflight、QEMU 命令渲染、display/gdb 参数、serial marker timeout 和 no-launch 行为。
- 最后更新 README、README-zh、AGENTS，以及必要的 docs/en 与 docs/zh 镜像文档。
- 回滚策略：保留 Bochs target 不受影响；如 QEMU backend 有问题，可移除或隐藏 QEMU target，现有 Bochs 工作流继续可用。

## Open Questions

暂无。
