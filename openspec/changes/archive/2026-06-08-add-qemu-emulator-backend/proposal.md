## Why

BigOS 当前本地启动调试和 smoke 验证只支持 Bochs，适合早期 x86 调试，但不利于 headless 自动化、快速串口 marker 回归和后续 CI-like 验证。接入 QEMU 作为并列 emulator backend，可以在不替换 Bochs、不改变 Legacy BIOS/MBR/exFAT 启动语义的前提下，提供更快、更易自动化的验证入口，并为未来 QEMU + OVMF 路线保留清晰边界。

## What Changes

- 为现有 boot debug helper 增加 QEMU emulator backend，复用当前 raw image 构建、exFAT 布局、串口 marker 和 no-launch/offline 校验能力。
- 新增与 Bochs 对等的 xmake run target：`xmake run qemu` 和 `xmake run qemu-gdb`；是否显示图形界面由 Python helper 的 `--display` 或等价参数控制，而不是新增独立 `qemu-headless` target。
- 保留 `xmake run bochs` 和 `xmake run bochs-sdl2` 的现有语义；QEMU 不替换 Bochs 默认调试链路。
- 更新项目文档和 Agent 指南：自动化 smoke、串口 marker、CI-like 验证优先使用 `qemu` 的 headless display 模式；普通快速本地启动验证优先使用 `qemu` 的图形 display 模式；早期 boot、BIOS、实模式/长模式切换和硬件行为差异排查仍使用或交叉验证 Bochs。
- 明确 QEMU 本 change 只覆盖当前 Legacy BIOS/MBR/exFAT 路径，不实现 UEFI loader、ESP 镜像或 OVMF 配置。
- 新增缺失 QEMU/Bochs/cross-toolchain 时的显式 preflight 报错与验证记录要求。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `one-command-boot-debug`: 将本地 boot debug workflow 从 Bochs-only 扩展为 Bochs + QEMU 并列 backend，新增 QEMU xmake 入口、QEMU GDB 调试入口、display 模式参数和文档化的 emulator 选择语义。
- `project-quality-assurance`: 更新 emulator smoke test 的项目级验证要求，规定 QEMU 的 headless display 模式作为自动化 smoke 的首选路径，并要求关键低层变更在可用时使用 Bochs 交叉验证。

## Impact

- 受影响代码：`tools/boot_debug.py`、`xmake.lua`、`tests/test_boot_debug.py` 或等价 helper 测试。
- 受影响文档：`README.md`、`README-zh.md`、`AGENTS.md`，必要时同步 `docs/en` 与 `docs/zh` 中的启动调试说明。
- 新增本地工具依赖：运行 QEMU backend 时需要 `qemu-system-x86_64`；Bochs backend 仍只要求 `bochs`。
- 架构假设：仅支持当前 x86_64 Legacy BIOS/MBR/exFAT 调试镜像；QEMU 磁盘 backend 必须保持 IDE/ATA PIO 兼容，不默认切换 virtio、AHCI/SATA 或 UEFI。
- 内存/地址假设：不修改 kernel link address、boot protocol、BootInfo/handoff、页表布局、kernel 初始化顺序或 smoke marker ABI。
- 非目标：不实现 UEFI/OVMF、ESP/FAT 镜像、preemptive scheduling、真实多进程、VFS/page cache 或新存储驱动。
