## 1. 安装器与构建保护

- [x] 1.1 修复 `install.py`，读取 MBR 分区表完整 64 字节，并校验全部四个分区表项。
- [x] 1.2 重构分区查找 helper，让 MBR、DBR、扩展 DBR 和 boot 安装共用同一条已校验的 exFAT 分区发现路径。
- [x] 1.3 实现 `--with-boot` 行为，只覆盖已有、连续、容量足够的 exFAT `/boot/boot.bin` 预分配文件。
- [x] 1.4 确保任何 DBR 或扩展 DBR 更新后都会刷新 exFAT main 和 backup boot-region checksum。
- [x] 1.5 增加构建或安装检查，在修改磁盘镜像前拒绝超大的 `mbr.bin`、`dbr.bin`、`exdbr.bin` 以及缺失、非连续、容量不足或目录布局不受支持的 `boot.bin` 放置。

## 2. 启动链正确性

- [x] 2.1 修复 `boot.cc` 中 exFAT `fileAttributes` bitmask 优先级问题。
- [x] 2.2 将 BIOS boot drive 从 `mbr.s` 一致传递到 `dbr_exfat.s` 和 `exdbr_exfat.s`。
- [x] 2.3 审计 `exdbr_exfat.s` 和 `boot.cc` 后续 ATA PIO 读取路径，并在无法直接使用 BIOS `DL` 时文档化或强制 primary-master 假设。
- [x] 2.4 为 `/boot/boot.bin` 和 `kernel` 增加明确 not-found 失败路径，避免越界或读取无效目录项。
- [x] 2.5 在可行范围内让 boot-stage 错误输出可见，包括用受支持的早期输出路径或文档化 halt code 替代 no-op boot C++ error printing。

## 3. 磁盘 IO 加固

- [x] 3.1 为 BIOS `int 13h` extended read 路径增加有界 retry/timeout 行为。
- [x] 3.2 为 `exdbr_exfat.s` 和 `boot.cc` 中的 ATA PIO polling loop 增加有界 retry/timeout 行为。
- [x] 3.3 读取端口数据前检查 ATA `ERR` 和 `DF` 状态，并在出现错误时以阶段级 disk-controller error halt。
- [x] 3.4 保持 timeout 和 status 常量具名并有文档，方便后续针对 emulator 调参。

## 4. ELF Loader

- [x] 4.1 加载前校验 ELF64 magic、class、endian、machine、header size 和 program-header metadata。
- [x] 4.2 读取足够扇区以覆盖完整 ELF program-header table。
- [x] 4.3 遍历每个 `PT_LOAD` program header，并按 `p_offset`、`p_filesz`、`p_memsz` 和目标地址加载 segment。
- [x] 4.4 进入 kernel 前，对每个 segment 的 `p_memsz - p_filesz` 范围执行 zero-fill。
- [x] 4.5 按文档化 bootloader、page-table 和 kernel 保留区域校验 segment 目标范围。
- [x] 4.6 校验 ELF `e_entry` 落在已加载 `PT_LOAD` 范围内，并让最终跳转使用已校验 entry point。

## 5. BootInfo 与地址布局

- [x] 5.1 在公共 x86 boot handoff 头中定义带版本的 canonical `BootInfo` ABI，包含 magic、version、size、handoff address、字段 offset、boot drive、E820 metadata、exFAT data-area metadata、kernel size 和 kernel entry/load 字段。
- [x] 5.2 在启动期间生成 `BootInfo`，同时保留 `0x500`、`0x800`、`0x80c` 及相关 consumer 的现有兼容写入。
- [x] 5.3 更新早期内存初始化，使其优先读取 `BootInfo`，仅将兼容别名作为文档化迁移路径使用。
- [x] 5.4 文档化早期 x86 boot 地址布局，包括各阶段 load address、stack、page table、directory buffer、boot handoff area 和 higher-half kernel base。
- [x] 5.5 为 boot C++、kernel C++ 和 assembly 使用的 `BootInfo` size、alignment、handoff address 和关键字段 offset 增加 `static_assert`、同源宏或构建期校验。

## 6. 验证

- [x] 6.1 使用预期 `x86_64-elf-*` 工具链运行 `arch/x86/boot` 的窄范围 boot build，或记录工具链不可用情况。记录：本机 `x86_64-elf-gcc`/`ld` 可见但编译/链接进程被 host `Killed: 9` 终止；已补充 assembler syntax 与 clang target syntax 检查。
- [x] 6.2 对修改后的 installer 代码运行 Python 检查：`uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`，或记录项目工具不可用情况。
- [x] 6.3 bootloader 变更后运行主项目构建（`xmake` 或文档化等价命令），或记录缺失 cross-toolchain 的限制。记录：`xmake` 运行后在 unrelated `kernel/irq/interrupt.s` 阶段因 host `Killed: 9` 终止。
- [x] 6.4 当 `test/bochsrc.bxrc` 和磁盘镜像已针对本机配置时，运行 Bochs boot smoke test。记录：本机未发现 `bochs`。
- [x] 6.5 在可行范围内验证负向场景：缺失 `boot.bin`、缺失 `kernel`、boot binary 超大，以及模拟 unsupported exFAT placement。
