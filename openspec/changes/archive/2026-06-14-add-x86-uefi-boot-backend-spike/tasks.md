## 1. UEFI 构建与源码骨架

- [x] 1.1 新增 x86_64 UEFI loader 源码目录和最小 UEFI ABI/header/glue，保持与现有 Legacy BIOS boot sector 源码隔离。
- [x] 1.2 新增 UEFI app 入口和基础诊断输出，确保 loader 能在 UEFI firmware console 或串口路径报告早期错误。
- [x] 1.3 新增 xmake UEFI 构建目标，使用本地 `clang` 和 `lld-link` 生成 `BOOTX64.EFI`，并将产物放入独立 build 输出目录。
- [x] 1.4 为 UEFI 构建目标加入 preflight，缺少 `clang`、`lld-link`、`llvm-objcopy`、`llvm-objdump` 或必要参数时明确失败。

## 2. Kernel ELF 加载与 handoff

- [x] 2.1 在 UEFI loader 中实现最小文件读取能力，从 ESP 读取 kernel ELF，失败时给出明确 loader 诊断。
- [x] 2.2 实现 UEFI loader 的 ELF64 校验与 loadable segment 加载，保持 kernel ELF 格式、entry 和地址假设与现有内核兼容。
- [x] 2.3 实现 UEFI `GetMemoryMap` 到 `BootMemoryRegion` 的保守转换，保留 source type、source value 和 attributes。
- [x] 2.4 实现 `BootInfo v2` blob 构造，core section 标记 UEFI boot protocol，memory map section 使用 UEFI 转换后的 `BootMemoryRegion`，并在 UEFI 路径将 `exfat_data_area_lba` 写为零值。
- [x] 2.5 定义并实现可选 storage metadata section，用于描述 UEFI ESP/root storage 来源，避免复用 Legacy exFAT 字段表达 UEFI 语义。
- [x] 2.6 定义并实现可选 loader metadata section，记录 UEFI backend、loader build id/version、firmware vendor/revision 和 ESP 路径等可审计信息。
- [x] 2.7 实现 `ExitBootServices` 前后的 bounded memory map refresh/retry，并在失败时停止进入 kernel。
- [x] 2.8 通过 x86_64 第一个参数寄存器传递 `BootInfoHeader*` 并跳转 kernel entry，保留 Legacy BIOS v1/v2 fallback 兼容性。

## 3. ESP 镜像与 QEMU/OVMF 调试入口

- [x] 3.1 扩展 Python helper 或新增等价 helper，用 `mformat`、`mmd`、`mcopy`、`mdir` 生成独立 ESP/FAT 镜像。
- [x] 3.2 将 `BOOTX64.EFI`、kernel ELF、`/boot/user/init.elf` 和默认 `/bin/*` 用户态程序打包进 ESP 镜像。
- [x] 3.3 新增 UEFI QEMU/OVMF 启动路径，复制 OVMF vars template 到 build 输出目录，避免修改包管理器安装目录。
- [x] 3.4 新增 UEFI headless 串口日志路径和 expected-marker 检查能力，默认等待与 Legacy BIOS 默认 headless 路径相同的 init/user exec marker（当前 baseline 为 `BIGOS_USER_EXEC`）。
- [x] 3.5 新增 xmake run target 或文档化 helper 入口，明确它是 UEFI/QEMU/OVMF 路径且不会替换现有 `qemu`、`qemu-gdb`、`bochs` 入口。

## 4. Legacy 回归保护与边界检查

- [x] 4.1 Review 现有 Legacy BIOS MBR/DBR/extended-DBR/`boot.bin` 产物路径，确认 UEFI 构建和镜像生成不会覆盖它们。
- [x] 4.2 Review kernel link address、entry ABI、`BootInfo` magic/version/size/alignment/offset、page-table 和早期内存地址假设，记录是否被本 change 改动。
- [x] 4.3 Review UEFI memory type 映射，确认 runtime、MMIO、ACPI、loader-owned、kernel-owned、bad、unknown 区域不会被加入初始 free page pool。
- [x] 4.4 Review UEFI loader 与 BIOS loader 的 ELF64 segment 加载规则差异，记录可接受差异和后续共享规则候选项。
- [x] 4.5 Review storage metadata 和 loader metadata sections 的 optional 语义，确认缺失这些 section 不会阻止 kernel 启动，未知 optional section 仍可跳过。

## 5. 文档与 OpenSpec 记录

- [x] 5.1 更新 `docs/en` 相关架构文档，说明 UEFI backend 是 x86_64 boot backend spike，不是第二 ISA，也不是默认 runtime-parity backend。
- [x] 5.2 更新 `docs/zh` 对应架构文档，保持与 `docs/en` 相同相对路径和相同语义。
- [x] 5.3 更新本地启动调试文档，分别说明 Legacy BIOS QEMU/Bochs 路径和 UEFI QEMU/OVMF 路径的适用范围。
- [x] 5.4 在双语文档中记录 `exfat_data_area_lba` 在 UEFI core section 中为零值、storage metadata section 和 loader metadata section 的语义。
- [x] 5.5 在双语文档中记录本地工具链假设：QEMU/OVMF、Homebrew LLVM/LLD、mtools、x86_64 cross toolchain，以及 Apple Silicon TCG 性能风险。
- [x] 5.6 确认 `docs/en` 和 `docs/zh` 对应语言镜像保持相同相对路径结构和等价内容。

## 6. 验证

- [x] 6.1 运行 `xmake`，确认现有 kernel 和 Legacy BIOS boot artifacts 仍可构建。
- [x] 6.2 运行 UEFI 构建目标，确认 `BOOTX64.EFI` 生成并用 `llvm-objdump` 或等价工具检查其 PE/COFF 形态。
- [x] 6.3 通过 `uv run ...` 执行 Python helper 的相关测试或校验；若修改 Python 文件，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`，不可用时记录 blocker。
- [x] 6.4 运行 Legacy BIOS QEMU headless smoke，确认现有可运行 baseline 未被 UEFI 路径破坏。
- [x] 6.5 运行 UEFI QEMU/OVMF headless smoke，记录是否观察到默认 init/user exec marker（当前 baseline 为 `BIGOS_USER_EXEC`）、串口日志路径、timeout 和残余风险。
- [x] 6.6 对 UEFI loader C/C++ 源码执行 clang/clangd 辅助静态检查，使用尽量接近 freestanding x86_64 的 flags，并区分历史诊断、当前变更诊断和工具链误报。
- [x] 6.7 如果 QEMU、OVMF、mtools、LLVM/LLD、cross toolchain 或 `uv` 不可用，记录跳过原因、替代检查和剩余 bootability 风险。
