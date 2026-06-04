## 1. 链接脚本实现

- [x] 1.1 修改 `link.lds`：将单一 `All PT_LOAD` 替换为 `text`、`rodata`、`data` 等具名 `PHDRS`，分别使用 `FLAGS(5)`、`FLAGS(4)`、`FLAGS(6)`。
- [x] 1.2 将 `.bigos`、`.init`、`.text`、`.fini` 映射到 RX `text` segment，并确认 `_start` 仍位于该 segment。
- [x] 1.3 将 `.rodata`、`.rodata1`、只读 `.eh_frame_hdr`、只读 `.eh_frame` 映射到只读 `rodata` segment。
- [x] 1.4 将 `.ctors`、`.dtors`、`.data`、`.4k_area`、`.bss` 映射到 RW `data` segment，避免改变当前 C++ runtime 表的可写性假设。
- [x] 1.5 在 text/rodata/data 权限类别边界插入 4KiB 对齐，并复核 `ENTRY(_start)`、higher-half 基址和 section 顺序未被意外改变。

## 2. Bootloader 兼容性复核

- [x] 2.1 复核 `src/arch/x86/boot/boot.cc` 对多个 `PT_LOAD` 的遍历、目标地址校验、文件范围校验、zero-fill 和 `kernel_memory_size` 计算。
- [x] 2.2 若 PHDR 拆分暴露 loader 边界问题，仅做针对性修复，保持磁盘读取、exFAT 查找、BootInfo handoff 和 paging 初始化架构不变。
- [x] 2.3 复核 entry point 校验仍覆盖拆分后的 RX segment，并确认最终跳转仍使用 ELF `e_entry`。

## 3. ELF 与地址布局验证

- [x] 3.1 运行 `xmake -r`，确认构建成功且 `LOAD segment with RWX permissions` warning 消失。
- [x] 3.2 运行 `x86_64-elf-readelf -l build/kernel` 或等价 cross binutils 检查，记录 `LOAD` segment 数量、权限、offset、virt addr、filesz、memsz。
- [x] 3.3 确认没有 `RWE` load segment，entry point 落在 RX `PT_LOAD`，所有 loadable segment virtual address 位于 `0xffffffff80000000` 及以上。
- [x] 3.4 复核拆分后 `kernel_memory_size` 的最大 segment end 仍覆盖 `.bss` 和 `.4k_area`，且未破坏早期内存初始化对 kernel image 范围的保守排除。

## 4. 启动与辅助检查

- [x] 4.1 在 Bochs/ROM/serial oracle 可用时运行有界 boot smoke，例如 `uv run python tools/boot_debug.py run --expect-serial-marker "BigOS kernel reached"`；不可用时记录具体限制和剩余 bootability 风险。
- [x] 4.2 若修改 `src/arch/x86/boot/boot.cc`，运行贴近交叉构建的 C++17 freestanding clang/clangd 辅助静态检查；若只修改 `link.lds`，记录 C++ 辅助检查不适用或无源码变更。
- [x] 4.3 运行 `uv run pytest`，确认现有源码断言和工具侧 marker 检查未被破坏。

## 5. 文档与 OpenSpec 验证

- [x] 5.1 更新架构文档，说明 kernel ELF text/rodata/data segment 权限布局、4KiB 对齐、`.ctors/.dtors` 本 change 保持 RW、验证命令和“未启用运行时页级 W^X”的非目标。
- [x] 5.2 记录后续页级权限收敛优先采用 linker 边界符号方案，直接消费 ELF `p_flags` 需要另起 BootInfo/loader metadata 设计。
- [x] 5.3 运行 `openspec validate split-kernel-load-phdrs`，修复 proposal/design/spec/tasks 结构问题。
- [x] 5.4 整理验证记录，分列已通过检查、无法运行的检查与原因、历史诊断、本次引入问题。

## 验证记录

已通过检查：

- `xmake -r`：通过；构建输出未出现 `LOAD segment with RWX permissions`。
- `x86_64-elf-objdump -p build/kernel`：通过；3 个 `LOAD` segment，分别为 `r-x`、`r--`、`rw-`；无 `rwx`。
- `x86_64-elf-objdump -f build/kernel`：通过；start address 为 `0xffffffff80000000`，位于第一个 RX `LOAD`。
- `x86_64-elf-objdump -h build/kernel`：通过；`.bigos/.init/.text/.fini` 位于 RX 范围，`.rodata/.eh_frame` 位于 R 范围，`.ctors/.dtors/.data/.4k_area/.bss` 位于 RW memory extent。
- `uv run pytest`：通过；31 passed。
- `openspec validate split-kernel-load-phdrs`：通过；change valid。
- VS Code diagnostics：通过；未报告诊断。

无法运行或受限检查：

- `x86_64-elf-readelf -l build/kernel` 与 `x86_64-elf-readelf -S build/kernel`：当前环境中退出码为 255 且无输出；已使用同一 cross binutils 套件的 `x86_64-elf-objdump -p/-h/-f` 作为等价 ELF 检查。
- `llvm-readelf -l build/kernel` 与 host `readelf -l build/kernel`：当前环境命令不存在。
- Bochs boot smoke：`uv run python tools/boot_debug.py run --expect-serial-marker "BigOS kernel reached"` 被用户中断/跳过；剩余风险是尚未在本次会话中用 serial oracle 证明拆分后的镜像能实际进入 kernel marker。

历史诊断：

- 初次 ELF section 检查发现当前源码使用 `.4k_align_area` input section，而旧 `link.lds` 只显式收集 `.4k_area`；已在 `.4k_area` output section 同时收集 `.4k_area` 和 `.4k_align_area`，避免 orphan section 改变预期顺序。
- `src/arch/x86/boot/boot.cc` 未修改；复核确认现有 loader 遍历所有 `PT_LOAD`、校验目标和文件范围、按最大 segment end 计算 `kernel_memory_size`、zero-fill `p_memsz - p_filesz`，并最终跳转到已校验的 ELF `e_entry`。

本次引入问题：

- 未发现新的构建、静态诊断或 Python 测试失败。
