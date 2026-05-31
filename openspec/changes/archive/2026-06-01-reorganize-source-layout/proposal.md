## Why

当前 BigOS 的实现代码分散在仓库顶层的 `arch/`、`kernel/`、`mm/`、`drivers/`、`cpp/` 和 `lib/src/` 中，随着 boot、IRQ、内存管理、驱动和 KTL 等子系统继续增长，根目录会越来越像源码目录而不是项目入口目录。现在整理项目组织结构，可以在不改变内核运行时行为的前提下，建立更清晰的源码边界，降低后续新增子系统、构建配置和文档维护的成本。

## What Changes

- 新增统一的实现源码布局约定：内核、驱动、内存管理、架构相关 boot 代码和运行时启动对象源码收敛到 `src/` 下。
- 保留顶层 `cpp/` 作为独立的 freestanding C++ 支撑库目录，继续包含 KTL、C++ ABI stub、`new`/`delete` 支撑代码及其专属 include root。
- 保留 `include/` 作为顶层内核公共头文件目录，避免内核公共接口、freestanding C header subset 和 C++ 支撑库接口混在一起。
- 将 `arch/x86/boot/install.py` 迁移到顶层 `tools/`，使开发辅助脚本与 boot runtime 源码解耦。
- 保留 `docs/`、`tests/`、`openspec/`、`.github/`、构建入口配置和链接脚本在顶层，维持项目资产的语义清晰。
- 更新 `xmake.lua`、`.clangd`、README、OpenSpec 项目说明、归档 OpenSpec change 和相关辅助配置中的路径引用。
- 对 boot、linker、runtime startup、disk image helper 和 Bochs 配置相关路径做单独核对，避免目录迁移破坏启动链路。
- 不改变硬件地址、内核链接地址、boot handoff 协议、页表布局、IRQ 向量、C++ ABI 支撑语义或任何内核运行时功能。

## Capabilities

### New Capabilities

- `source-layout-organization`: 规定 BigOS 仓库中实现源码、公共头文件、工具脚本、文档、测试和构建入口的组织边界，并要求构建、IDE 和文档路径与该布局保持一致。

### Modified Capabilities

- 无。

## Impact

- 影响源码路径：`arch/x86/boot`、`kernel`、`mm`、`drivers`、`lib/src`。
- 影响 C++ 支撑库边界：顶层 `cpp/`、`cpp/include`、`cpp/ktl` 和 `cpp/libsupc++` 保留为独立 C++ support library，不迁入 `src/` 或顶层 `include/`。
- 影响工具路径：`arch/x86/boot/install.py` 将迁移到 `tools/`，并需要更新 Python 配置和文档。
- 影响构建和开发配置：`xmake.lua`、`.clangd`、可能的 `compile_commands.json` 生成流程、`pyrightconfig.json`、顶层 `Makefile` 和 boot 局部 `Makefile` 中的路径引用。
- 影响文档和规范：`README.md`、`README-zh.md`、`AGENTS.md`、`openspec/config.yaml`、既有 specs 和归档 change 中的路径引用说明。
- 影响验证范围：需要运行或记录 `xmake` 构建、C++/assembly 静态检查、Python 工具检查，以及在 Bochs 和本机路径可用时的 boot smoke test。
- 架构假设：目标仍为 x86_64 freestanding kernel，boot 仍从 x86 磁盘镜像加载名为 `kernel` 的 ELF64 内核。
- 内存和 ABI 假设：高半区内核链接地址仍为 `0xffffffff80000000`，不调整 boot handoff 数据结构、页表布局、startup object 顺序或 C++ ABI stub 行为。
- 工具链和模拟器假设：继续以 `xmake` 和 `x86_64-elf-gcc`/`x86_64-elf-g++` 为主验证路径；Bochs smoke test 仅在本机 Bochs 与 `test/bochsrc.bxrc` 可用时执行。
- 非目标：不实现调度器、进程、用户态、系统调用、文件系统服务、新驱动、TTY/console 抽象或新的内存管理功能。
