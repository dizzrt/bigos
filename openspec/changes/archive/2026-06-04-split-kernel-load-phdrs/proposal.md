## Why

当前 `link.lds` 将 `.text`、`.rodata`、`.data`、`.bss` 等全部放入同一个 `PT_LOAD`，导致 `x86_64-elf-ld` 报告 `build/kernel has a LOAD segment with RWX permissions`。这会隐藏代码段、只读数据段和可写数据段的权限边界，也不利于后续页表权限收敛与 W^X 约束。

## What Changes

- 将 kernel ELF 链接脚本从单一 `All PT_LOAD` 方案调整为多个具名 `PHDRS`，使可执行、只读和可写段拥有不同 program header 权限。
- 保持 higher-half kernel base、`ENTRY(_start)`、现有 section 顺序、符号可见性和 boot handoff ABI 不变。
- 保持 bootloader 对多个 `PT_LOAD` 的加载契约，并在实现时用 ELF program header 检查和启动 smoke 验证它能继续加载拆分后的 kernel。
- 增加文档说明 kernel ELF segment 权限布局、架构假设和验证方法。
- 非目标：不引入用户态权限、不改变页表映射策略、不切换到 UEFI/OVMF、不重构 bootloader 文件系统或磁盘读取路径。

## Capabilities

### New Capabilities

- `kernel-elf-segment-layout`: 约束 kernel ELF 的 `PT_LOAD` 拆分、权限边界、地址布局兼容性和验证要求。

### Modified Capabilities

- `x86-bootloader-hardening`: 明确现有 x86 BIOS bootloader 多 `PT_LOAD` 加载契约必须覆盖权限拆分后的 kernel ELF。

## Impact

- 影响子系统：x86 kernel 链接脚本、Legacy BIOS ELF64 kernel 加载路径、构建/启动验证文档。
- 主要代码：`link.lds`、`kernel/arch/x86/boot/boot.cc`（仅复核或必要时修复多 `PT_LOAD` 加载）、相关架构文档。
- 架构假设：目标仍为 x86_64 ELF64 higher-half kernel，虚拟基址保持 `0xffffffff80000000`，现有 bootloader 按 `PT_LOAD` program header 加载 kernel。
- 内存布局假设：拆分 program header 不改变 section 的相对顺序、入口地址、运行时符号和早期内存初始化依赖的 kernel image 范围。
- 工具链假设：使用 `x86_64-elf-ld` 生成 ELF64；验证通过 `readelf`/`objdump` 等 cross binutils 检查 program header 权限。
- 模拟器假设：Bochs smoke 若本机 GUI/serial oracle 不可用，需要记录原因与剩余 bootability 风险。
