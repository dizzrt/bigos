## 1. 现状确认与边界审查

- [x] 1.1 审查当前 `link.lds`、`build/kernel` program headers 和 `xmake` 链接输出，确认当前回归为单个 `RWE PT_LOAD`。
- [x] 1.2 审查 `kernel/arch/x86/boot` ELF64 loader，确认它按所有 `PT_LOAD` 遍历加载、zero-fill 并计算 kernel memory extent，而不是隐含单段假设。
- [x] 1.3 确认本 change 不修改 higher-half base、`_start` entry、BootInfo handoff、MBR/exFAT/ATA PIO、IDT/syscall vector、GDT/TSS、CR3 切换或页表 self-map 地址。

## 2. Linker Script 实现

- [x] 2.1 修改 `link.lds`，恢复 `text PT_LOAD FLAGS(5)`、`rodata PT_LOAD FLAGS(4)`、`data PT_LOAD FLAGS(6)` 三类 program headers。
- [x] 2.2 将 `.bigos`、`.init`、`.text`、`.fini` 放入 RX text segment，并保证 `_start` entry 仍落在 executable `PT_LOAD` 内。
- [x] 2.3 将 `.rodata`、`.rodata1`、只读 `.eh_frame_hdr` 和只读 `.eh_frame` 放入只读 rodata segment。
- [x] 2.4 将 `.ctors`、`.dtors`、`.data`、`.4k_area`、`.bss` 放入 RW data segment，并保持 `.bss` / `p_memsz > p_filesz` zero-fill 语义可由 bootloader 观察。
- [x] 2.5 保证 text/rodata/data 权限边界 4 KiB 对齐，不让同一 4 KiB 页同时承载需要 executable 和 writable 权限的内容。

## 3. 防回归检查

- [x] 3.1 补充或更新源码级测试，检查 `link.lds` 不再使用单个 `kernel PT_LOAD FLAGS(7)` 覆盖所有 kernel sections。
- [x] 3.2 补充或更新源码级测试，检查 `link.lds` 包含 text/rodata/data 三类 `PT_LOAD` 权限，并检查关键 sections 映射到预期 segment。
- [x] 3.3 补充或更新 ELF artifact 检查，使用 `x86_64-elf-readelf -lW build/kernel` 或等价命令验证无 `RWE` LOAD，且存在 `R E`、`R`、`RW` loadable segments。
- [x] 3.4 如检查使用 Python 测试或 helper，命令必须通过 `uv run ...` 执行；若 `uv` 不可用，明确记录阻塞原因。

## 4. 构建与启动验证

- [x] 4.1 运行或记录无法运行的 `xmake`，确认链接阶段不再输出 `LOAD segment with RWX permissions`。
- [x] 4.2 运行或记录无法运行的 `x86_64-elf-readelf -lW build/kernel` 和 `x86_64-elf-objdump -f build/kernel`，确认权限拆分和 entry point 未变化。
- [x] 4.3 运行或记录无法运行的 QEMU headless boot smoke，验证 Legacy BIOS image 能从多 `PT_LOAD` kernel ELF 启动并到达现有串口 marker。
- [x] 4.4 在本地 Bochs/ROM/display/serial oracle 可用时运行 Bochs smoke；不可用时记录跳过原因和剩余 bootability 风险。
- [x] 4.5 对修改的 linker/boot 相关文件执行或记录无法执行的辅助静态检查；若本 change 不修改 C++ 源码，说明 clang/clangd 不适用或仅作为辅助信号。

## 5. 文档与记录

- [x] 5.1 检查 `docs/en/arch/x86-boot-layout.md` 与 `docs/zh/arch/x86-boot-layout.md` 是否仍准确描述当前三段权限布局；如实现细节有变，同步更新双语文档。
- [x] 5.2 运行或记录无法运行的 `openspec validate split-kernel-load-segments --strict`，修复当前 change 引入的格式或规格问题。
- [x] 5.3 在验证记录中分开列出已通过检查、因工具链/模拟器/ROM/display/serial oracle 不可用而跳过的检查、替代检查、历史问题和当前 change 引入的问题。

## 验证记录

- 已通过：变更前 `xmake` 可复现 linker warning `LOAD segment with RWX permissions`，且 `x86_64-elf-readelf -lW build/kernel` 显示单个 `RWE` `LOAD` 覆盖 `.bigos/.init/.text/.fini/.ctors/.dtors/.rodata/.eh_frame/.data/.4k_area/.bss`。
- 已通过：复核并修复 `kernel/arch/x86/boot/boot.cc`，确认 loader 读取完整 program header table，缓存 `phnum`/`phoff` 后遍历所有 `PT_LOAD`，按最大 segment end 计算 `kernel_memory_size`，逐段加载并 zero-fill `p_memsz > p_filesz`，最终跳转到已校验的 ELF `e_entry`。
- 已通过：`xmake`；构建成功，链接阶段不再输出 `LOAD segment with RWX permissions`。
- 已通过：`x86_64-elf-readelf -lW build/kernel`；3 个 `LOAD` 分别为 `R E`、`R`、`RW`，无 `RWE`，entry point 为 `0xffffffff80000000`。
- 已通过：`x86_64-elf-objdump -f build/kernel`；start address 保持 `0xffffffff80000000`。
- 已通过：`x86_64-elf-objdump -h build/kernel`；`.bigos/.init/.text/.fini` 位于 RX 范围，`.rodata/.eh_frame` 位于 R 范围，`.ctors/.dtors/.data/.4k_area/.bss` 位于 RW memory extent。
- 已通过：`uv run pytest tests/test_kernel_elf_segment_layout.py`，3 项通过；Python 验证通过 `uv run` 执行。
- 已通过：`openspec validate split-kernel-load-segments --strict`。
- 已通过：`uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log build/test/bochs-post-clean.log --expect-serial-marker "BIGOS_USER_EXEC" --smoke-timeout 20`，fresh Bochs 镜像到达默认 init/user exec marker。
- 已通过：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/qemu-post-clean.log --expect-serial-marker "BIGOS_USER_EXEC" --smoke-timeout 20`，fresh QEMU 镜像到达默认 init/user exec marker。
- 已通过：VS Code diagnostics；未报告诊断。
- 跳过：无工具链缺失；`uv`、`x86_64-elf-readelf`、`x86_64-elf-objdump`、QEMU、Bochs 均可用。
- 替代/诊断检查：临时 bootloader 串口插桩证明修复前仅加载 `idx=0` 的 RX segment，RW 段开头 `.ctors` 为全 0；修复后加载 `idx=0/1/2` 三个 `PT_LOAD`，RW 段开头恢复为 `0xffffffffffffffff` 和有效 constructor 地址。临时插桩已清理。
- 历史/既有问题：boot artifact 构建仍输出 `mbr.s`、`dbr_exfat.s` 的 `movsd`/`movsl` assembler warning。
- 当前 change 新增问题：无已知。
