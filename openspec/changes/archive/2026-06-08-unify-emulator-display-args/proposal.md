## Why

当前本地启动调试入口同时存在 `bochs`、`bochs-sdl2`、`qemu`、`qemu-gdb`，其中 Bochs 的 SDL2 选择通过 target 名称表达，QEMU 的图形/无头选择通过 Python helper 的 `--display` 表达，xmake wrapper 又无法转发 `--` 后的运行参数。这让日常命令形态不一致，也让 Bochs 无头模式等 display 能力需要继续新增 target 或要求开发者绕过 xmake 直接调用 helper。

本 change 统一 emulator display 参数模型，让 `xmake run <target> -- ...` 成为稳定的本地启动调试参数入口，同时保持现有 Legacy BIOS/MBR/exFAT 镜像、ATA PIO 磁盘路径、bootloader、kernel handoff、linker 地址和内核运行时初始化语义不变。

## What Changes

- **BREAKING**: 取消 `xmake run bochs-sdl2` 和 `bochs-sdl2` emulator backend，Bochs 仅保留 `xmake run bochs` / `--emulator bochs`。
- `xmake run bochs` 默认使用 SDL2 图形 display，等价于显式传入 `xmake run bochs -- --display sdl2`。
- `xmake run bochs -- --display none` 使用 Bochs 无图形配置，映射到 Bochs 支持的 no-GUI display library。
- `xmake run qemu`、`xmake run qemu-gdb`、`xmake run bochs` 支持将 `--` 后的 target arguments 转发给 `tools/boot_debug.py run`。
- `tools/boot_debug.py` 将 `--display` 从 QEMU 专属参数改造成 emulator 通用参数，并按 backend 校验允许值。
- QEMU 继续支持 `--display graphical` 和 `--display none`；Bochs 支持 `--display sdl2` 和 `--display none`。
- 更新 `README.md`、`README-zh.md`、`AGENTS.md`、相关 `docs/en` 与 `docs/zh` 文档、OpenSpec specs 和测试，移除 `bochs-sdl2` 稳定入口说明并记录新的命令形态。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `one-command-boot-debug`: 将 Bochs SDL2 target 契约迁移为单一 Bochs backend + 通用 `--display` 参数，并要求 xmake run target 转发 `--` 后参数。
- `project-quality-assurance`: 更新 QEMU/Bochs smoke/debug 场景优先级和受支持入口说明，移除 `bochs-sdl2` 作为稳定验证入口。

## Impact

- 受影响子系统：开发工具链、本地 boot debug workflow、OpenSpec 质量验证约定和文档；不影响 kernel runtime 子系统。
- 受影响文件：`tools/boot_debug.py`、`xmake.lua`、`tests/test_boot_debug.py`、`README.md`、`README-zh.md`、`AGENTS.md`、`docs/en/**`、`docs/zh/**`、`openspec/specs/**`。
- API/CLI 影响：`xmake run bochs-sdl2` 被移除；`xmake run bochs -- --display sdl2|none` 成为 Bochs display 选择入口；`xmake run qemu -- --display none` 和 `xmake run qemu-gdb -- --display none` 成为 xmake wrapper 下的 QEMU display 选择入口。
- Emulator 假设：QEMU 使用 Legacy BIOS 默认路径和 IDE raw disk；Bochs 使用生成的 `bochsrc`，`sdl2`/`nogui` display library 是否可运行取决于本机 Bochs 编译/安装能力。
- 架构与磁盘布局假设：继续使用 x86_64 Legacy BIOS/MBR/exFAT raw image，继续通过现有 ATA PIO 兼容路径暴露磁盘，不引入 UEFI、OVMF、ESP/FAT、virtio、AHCI/SATA、NVMe 或新 storage driver。
- Toolchain 假设：继续使用 xmake、Python helper、`x86_64-elf-*` 交叉工具链；Python 相关验证通过 `uv run ...` 执行。
