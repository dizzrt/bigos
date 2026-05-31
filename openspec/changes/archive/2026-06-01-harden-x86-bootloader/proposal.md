## 背景与动机

当前 x86 启动路径已经能够在较窄的磁盘镜像布局下启动，但仍存在多个脆弱点：安装脚本尚未真正安装 `boot.bin`，分区表扫描不完整，启动盘编号没有在各阶段一致传递，ELF/exFAT 加载器也假设文件和设备都处于理想状态。这个 change 的目标是在更多内核内存管理和运行时工作依赖该路径之前，先加固 BIOS/MBR/exFAT 启动链。

## 变更内容

- 修复启动栈的磁盘镜像安装流程：
  - 定位 exFAT 分区时读取全部四个 MBR 分区表项。
  - 实现将 `boot.bin` 覆盖写入已有、连续、容量足够的 `/boot/boot.bin` 预分配文件。
  - 对缺失、非连续、容量不足或目录布局不受支持的 `boot.bin` 放置方式明确失败，不在本 change 中分配或更新 exFAT 目录项。
  - 修改 boot sector 时保留并更新 exFAT boot-region checksum。
- 提升启动正确性和错误处理：
  - 修复查找 `kernel` 时使用的 exFAT 文件属性判断。
  - 将 BIOS boot drive 从 MBR 到 DBR、扩展 DBR 以及后续读盘路径中一致传递。
  - 为 `boot.bin` 和 `kernel` 增加明确的文件未找到路径。
  - 为 ATA/BIOS 读盘失败增加超时和错误报告。
- 改进 ELF64 加载：
  - 加载所有相关 `PT_LOAD` program header，不再假设只有一个 load segment。
  - 正确处理 segment 的文件大小、内存大小、对齐和 zero-fill 要求。
  - 校验并跳转到 ELF `e_entry`，不再把固定 higher-half base 作为长期入口规则。
- 定义并文档化早期启动 handoff 数据：
  - 在公共 x86 boot handoff 头中引入 canonical `BootInfo` ABI，替代当前散落在固定低地址中的值。
  - 文档化 MBR、DBR、扩展 DBR、`boot.bin`、页表和 kernel handoff 使用的早期物理/虚拟地址布局。
- 增加构建期安全检查：
  - 校验 MBR/DBR 镜像必须适配单扇区。
  - 校验扩展 DBR 必须适配预留的 boot-region 扇区。
  - 安装前校验 bootloader 镜像大小和保留内存假设。

## 能力范围

### 新增能力

- `x86-bootloader-hardening`：定义 legacy BIOS x86 启动路径的可靠性、安装、handoff、ELF 加载和构建校验行为。

### 修改能力

- 无。

## 影响范围

- 受影响子系统：x86 BIOS 启动和早期内核 handoff。
- 受影响代码：
  - `tools/install.py`
  - `src/arch/x86/boot/mbr.s`
  - `src/arch/x86/boot/dbr_exfat.s`
  - `src/arch/x86/boot/exdbr_exfat.s`
  - `src/arch/x86/boot/boot.s`
  - `src/arch/x86/boot/boot.cc`
  - `src/arch/x86/boot/Makefile`
  - 消费启动 handoff 数据的早期内存初始化代码，尤其是 `src/mm/buddy.cc`
- 基本假设：
  - 架构仍为 x86/x86_64 legacy BIOS boot。
  - 内核仍是链接到 `0xffffffff80000000` 附近的 higher-half ELF64 镜像，但最终入口由已校验的 ELF `e_entry` 决定。
  - 支持的磁盘镜像使用 MBR 加 exFAT 分区。
  - 支持的 bootloader 安装镜像已经包含预分配的 `/boot/boot.bin`。
  - `x86_64-elf-gcc`、`x86_64-elf-ld`、GNU binutils、Python 3 和 Bochs 仍是预期本地工具链/模拟器路径。
- 非目标：
  - 不新增 UEFI 启动路径。
  - 不新增 AHCI/NVMe/USB/virtio 块设备驱动。
  - 不实现完整 exFAT 文件系统，只覆盖 bootloader 所需能力。
  - 不扩展调度器、用户态、文件系统服务或内核驱动体系。
