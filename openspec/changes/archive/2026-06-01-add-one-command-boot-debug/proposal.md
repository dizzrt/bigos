## Why

BigOS 目前只能分别运行内核构建、boot 局部构建、镜像写入和 Bochs 启动命令，缺少一个可重复的一行命令来完成启动调试准备。随着 boot、ELF 加载、内存初始化和 IRQ 初始化继续演进，开发者需要一个不依赖 macOS `diskutil`、挂载权限或手工镜像准备的本地启动调试入口。

## What Changes

- 新增一行命令启动调试能力，第一阶段目标是本地 Bochs 调试路径，而不是 CI 级自动判定。
- 新增用户态 raw disk image 生成流程，由脚本直接生成固定布局镜像并写入 MBR、exFAT 结构、`/boot/boot.bin` 和 `/kernel`。
- 将编译、打包、镜像生成/刷新、boot 扇区写入和 Bochs 启动串联成单个开发者命令。
- 保留现有 bootloader 查找约定：exFAT 分区中存在 `/boot/boot.bin`，根目录存在名为 `kernel` 的 ELF64 文件。
- 提供前置环境检查和清晰失败信息，覆盖 `xmake`、`x86_64-elf-*`、`bochs`、Python 运行环境、构建产物和镜像写入步骤。
- 不引入 QEMU 作为第一阶段默认模拟器；QEMU/headless/串口自动判定留给后续阶段。
- 不改变 boot 地址、内核链接地址、ELF 加载协议、`BootInfo` 布局、exFAT 查找语义或内核运行时初始化顺序。

## Capabilities

### New Capabilities
- `one-command-boot-debug`: 覆盖从构建到 raw 磁盘镜像生成、boot/kernel 写入和 Bochs 启动的本地一行命令调试能力。

### Modified Capabilities
- 无。

## Impact

- 受影响子系统：boot 构建与安装工具、开发者启动调试流程、测试/本地镜像资产生成流程。
- 主要代码位置：`tools/` 中新增或扩展启动调试脚本和 raw image/exFAT 写入逻辑；`kernel/arch/x86/boot/Makefile`、顶层 `Makefile` 或 `xmake.lua` 可增加便捷入口但不改变现有构建语义。
- 架构假设：目标为 x86_64 BIOS 启动路径，保留 MBR、exFAT DBR、extended DBR、`boot.bin`、ELF64 `kernel` 的链路。
- 内存布局假设：保留 `boot.s`/`boot.cc` 现有加载地址和高半区内核地址 `0xffffffff80000000`。
- 磁盘布局假设：脚本生成固定 raw disk image，包含一个 exFAT 分区、`/boot/boot.bin` 文件和根目录 `kernel` 文件，并满足现有 bootloader 只支持的连续文件/目录约束。
- 模拟器假设：第一阶段使用 Bochs，依赖本机安装 `bochs`；host-specific Bochs BIOS/VGA BIOS 配置需要由脚本生成默认配置或明确提示。
- 工具链假设：依赖 `xmake`、`x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld`、`x86_64-elf-as` 和 Python 3。
- 非目标：不实现 CI headless 判定、不新增串口日志协议、不替换 Bochs 为 QEMU、不修改内核功能、不实现完整 exFAT 文件系统工具。
