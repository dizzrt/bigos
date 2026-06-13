## Purpose

Define the required hardening behavior for the supported x86 BIOS/MBR/exFAT boot path, including deterministic installation, bounded disk IO, ELF64 loading, BootInfo handoff, and fixed early address layout guarantees.

## Requirements

### Requirement: 确定性的启动镜像安装

启动安装器 SHALL 以确定性方式安装或拒绝受支持 BIOS/MBR/exFAT 启动路径所需的每个 x86 boot artifact。

#### Scenario: 扫描所有 MBR 分区表项

- **WHEN** 安装器在 MBR 中查找 exFAT 分区
- **THEN** 它 MUST 读取并检查全部四个 16 字节分区表项

#### Scenario: boot.bin 安装行为明确

- **GIVEN** 目标镜像包含已有、连续、容量足够的 `/boot/boot.bin`
- **WHEN** 使用 `--with-boot` 调用 `tools/install.py`
- **THEN** 它 MUST 将生成的 `boot.bin` 覆盖写入该预分配文件的数据区
- **AND** 它 MUST NOT 新建文件、扩展文件、分配 cluster、更新 allocation bitmap，或生成新的 exFAT 目录项

#### Scenario: boot.bin 放置方式不受支持

- **WHEN** `/boot/boot.bin` 缺失、非连续、容量不足，或目录布局不受支持
- **THEN** 安装器 MUST 以明确的 unsupported-layout 或等价错误失败
- **AND** 它 MUST NOT 部分更新磁盘镜像

#### Scenario: 刷新 exFAT boot checksum

- **WHEN** 安装器修改 exFAT boot-region 扇区
- **THEN** 它 MUST 更新对应 main 和 backup boot-region checksum

### Requirement: 构建期二进制大小校验

启动构建流程 SHALL 拒绝超过其预留磁盘区域或内存启动区域的 boot binary。

#### Scenario: 扇区启动代码超过单扇区

- **WHEN** `mbr.bin` 或 `dbr.bin` 超过 512 字节
- **THEN** 构建或安装步骤 MUST 在写入磁盘镜像前失败

#### Scenario: 扩展 DBR 超过预留扇区

- **WHEN** `exdbr.bin` 超过预留 extended DBR sector range
- **THEN** 构建或安装步骤 MUST 在写入磁盘镜像前失败

#### Scenario: boot.bin 超过受支持放置空间

- **WHEN** `boot.bin` 大于受支持目标文件分配空间或预留加载假设
- **THEN** 安装器 MUST 在不部分更新磁盘镜像的情况下失败

### Requirement: 一致的 boot drive 传递

启动链 SHALL 在 MBR、DBR、扩展 DBR 和后续读盘路径中一致使用 BIOS 提供的 boot drive。

#### Scenario: 保留 BIOS boot drive

- **WHEN** BIOS 以 `DL` 中的 boot drive 进入 MBR
- **THEN** 后续每个启动阶段 MUST 使用该 boot drive 值执行 BIOS 磁盘读取，除非后续阶段显式验证并覆盖它

#### Scenario: ATA-only 阶段记录驱动器假设

- **WHEN** 后续阶段使用 ATA primary-master PIO 而不是 BIOS `DL`
- **THEN** 该阶段 MUST 文档化该假设，并在无法满足 boot drive 契约时明确失败

### Requirement: 正确的 exFAT 文件选择

bootloader SHALL 只为 `boot.bin` 和 `kernel` 选择有效的 exFAT 文件项。

#### Scenario: 正确计算文件属性判断

- **WHEN** bootloader 检查 exFAT file directory entry
- **THEN** 它 MUST 使用显式 bitmask 分组计算 archive/file attribute，再将该 entry 视为文件

#### Scenario: boot.bin 缺失

- **WHEN** 在受支持 exFAT 目录布局中找不到 `/boot/boot.bin`
- **THEN** 扩展 DBR MUST 报告 `bootloader not found` 或等价阶段级错误并 halt

#### Scenario: kernel 缺失

- **WHEN** 在受支持 exFAT 目录布局中找不到 `kernel` 文件
- **THEN** `boot.bin` MUST 报告阶段级 kernel-not-found 错误并 halt，不得读取无效目录项

### Requirement: 有界磁盘读取失败处理

bootloader SHALL 限制磁盘等待循环，并以阶段级错误暴露磁盘读取失败。

#### Scenario: BIOS 磁盘读取失败

- **WHEN** BIOS `int 13h` extended read 返回错误
- **THEN** 当前启动阶段 MUST 打印 disk-read failure 并 halt

#### Scenario: ATA 设备始终未就绪

- **WHEN** ATA PIO status polling 在配置重试限制前未观察到 ready data state
- **THEN** bootloader MUST 停止 polling、报告 timeout 并 halt

#### Scenario: 观察到 ATA error bit

- **WHEN** ATA PIO status 报告 `ERR` 或 `DF`
- **THEN** bootloader MUST 报告 disk-controller failure 并 halt，不得从端口消费数据

### Requirement: ELF64 program-header 加载

bootloader SHALL 按照 ELF64 的 `PT_LOAD` program header 加载 kernel，并 MUST 支持由 kernel 链接脚本生成的多个不同权限 loadable segment。bootloader MUST NOT 假设 kernel ELF 只有单个 `PT_LOAD`，也 MUST NOT 因 text/rodata/data 被拆分到不同 program header 而遗漏加载、遗漏 zero-fill 或错误计算 kernel memory extent。实现和验证 MUST 覆盖当前 kernel linker script 产生的多 `PT_LOAD` layout，而不是只覆盖合成测试镜像。

#### Scenario: ELF header 无效

- **WHEN** kernel 文件不包含受支持的 ELF64 x86_64 header
- **THEN** bootloader MUST 报告 invalid-kernel 错误并 halt

#### Scenario: 存在多个 loadable segment

- **WHEN** ELF64 program header table 包含多个 `PT_LOAD` entry
- **THEN** bootloader MUST 将每个 loadable segment 加载到预期映射目标

#### Scenario: 权限拆分后的多个 loadable segment

- **WHEN** kernel ELF 将 text、rodata 和 data 拆分为多个不同权限的 `PT_LOAD` entry
- **THEN** bootloader MUST 遍历并加载所有 loadable segment
- **AND** bootloader MUST continue to compute kernel memory extent from the maximum end address of all loaded segments

#### Scenario: 当前 kernel artifact 使用多段 layout 启动

- **WHEN** `build/kernel` 由项目 `link.lds` 生成多个 `PT_LOAD` program headers
- **THEN** Legacy BIOS boot image generation MUST package that artifact without rewriting it into a single loadable segment
- **AND** the bootloader MUST reach the existing kernel entry marker when emulator validation is available

#### Scenario: segment 内存大小大于文件数据

- **WHEN** 某个 `PT_LOAD` segment 的 `p_memsz` 大于 `p_filesz`
- **THEN** bootloader MUST 在进入 kernel 前 zero-fill 剩余 segment memory

#### Scenario: program header 跨多个扇区

- **WHEN** ELF64 program header table 跨越超过一个磁盘扇区
- **THEN** bootloader MUST 读取足够扇区以解析所有声明的 program header

#### Scenario: 使用 ELF entry point

- **WHEN** ELF64 header 提供 `e_entry`
- **THEN** bootloader MUST 校验 entry point 落在已加载的 `PT_LOAD` 虚拟地址范围内
- **AND** bootloader MUST 跳转到已校验的 ELF entry point，而不是固定 higher-half base

### Requirement: 带版本的 BootInfo handoff

启动链 SHALL 通过带版本的 `BootInfo` 契约暴露早期启动元数据。

canonical `BootInfo` ABI SHALL 定义在公共 x86 boot handoff 头中，并暴露 assembly、boot C++ 和 kernel C++ 可共享或可校验的 magic、version、handoff address、字段 offset 和 C-compatible struct 布局。

#### Scenario: 生成 BootInfo

- **WHEN** bootloader 进入 kernel
- **THEN** documented handoff address 处 MUST 存在包含 magic、version、size、boot drive、E820 metadata、kernel size 和相关磁盘布局元数据的 `BootInfo` 结构

#### Scenario: 现有内存分配器读取启动元数据

- **WHEN** 早期物理内存初始化消费 E820 和 kernel-size 数据
- **THEN** 它 MUST 读取文档化 `BootInfo` 字段，或在迁移期间读取文档化兼容别名

#### Scenario: BootInfo 版本不受支持

- **WHEN** kernel 观察到不受支持的 `BootInfo` magic、size 或 version
- **THEN** 它 MUST 早期失败，或使用显式文档化兼容路径，而不是静默解释未知数据

#### Scenario: BootInfo 布局校验

- **WHEN** 构建 boot C++ 或 kernel C++ consumer
- **THEN** 构建 MUST 校验 `BootInfo` 的 size、alignment 和关键字段 offset 与 canonical ABI 常量一致
- **AND** assembly 使用的 handoff address 和字段 offset MUST 与 canonical ABI 保持同步或被构建检查覆盖

### Requirement: 文档化早期地址布局

x86 启动路径 SHALL 文档化 boot assembly、boot C++ 和早期 kernel code 依赖的固定早期地址和保留区域。

#### Scenario: 地址布局变化

- **WHEN** boot-stage load address、page-table address、handoff address、stack address 或 kernel virtual base 发生变化
- **THEN** 对应文档和构建期校验 MUST 在同一 change 中更新

#### Scenario: 保留区域重叠

- **WHEN** 构建期校验发现生成的 boot artifact 或 page-table reservation 与文档化保留区域重叠
- **THEN** 构建 MUST 在产出或安装 boot image 前失败

### Requirement: Legacy BIOS 路径保持为统一 handoff producer

现有 x86 BIOS/MBR/exFAT 启动路径 SHALL 在 UEFI 蓝图阶段继续保持可用，并作为统一 kernel handoff 的一个 producer 进行规划。

#### Scenario: BIOS boot hardening 不被 UEFI 蓝图破坏

- **WHEN** UEFI 蓝图 change 被应用
- **THEN** 现有 BIOS/MBR/exFAT 启动路径的 boot artifact、地址布局、ELF64 加载和 `BootInfo` 生成语义 MUST 保持不变

#### Scenario: BIOS 路径参与 handoff 演进

- **WHEN** 后续 change 扩展 `BootInfo` 或统一 memory map 契约
- **THEN** BIOS backend MUST 明确如何填充新契约或如何保留文档化 fallback
- **AND** 它 MUST NOT 静默生成与 kernel consumer 不匹配的 handoff 数据

#### Scenario: BIOS 路径适配寄存器 handoff

- **WHEN** 后续 change 将 kernel entry ABI 改为寄存器传递 `BootInfo*`
- **THEN** BIOS backend MUST 明确如何设置该寄存器并保留必要的 Legacy 低地址 fallback
- **AND** 它 MUST NOT 只依赖固定低地址作为长期 handoff ABI

### Requirement: 固件差异不得扩散到 kernel 子系统

x86 bootloader hardening SHALL 把 BIOS 固件专用输入限制在 boot backend 内，并通过统一 handoff 将规范化结果交给 kernel。

#### Scenario: BIOS 固件数据被规范化

- **WHEN** BIOS backend 收集 E820、boot drive、磁盘布局或视频相关启动数据
- **THEN** 它 MUST 通过文档化 handoff 字段暴露 kernel 需要的数据
- **AND** kernel 子系统 MUST NOT 新增对 BIOS interrupt 调用路径的依赖

#### Scenario: BIOS 固定地址新增依赖受控

- **WHEN** 后续 BIOS boot change 需要新增固定低地址、page-table reservation 或 handoff alias
- **THEN** 该 change MUST 更新地址布局文档和构建期校验策略
- **AND** 它 MUST 说明该新增依赖与未来 UEFI backend 的兼容关系

### Requirement: Legacy BIOS backend supports unified handoff migration

现有 x86 Legacy BIOS bootloader SHALL 在保持当前可启动路径兼容的同时，作为统一 boot handoff ABI 的 v2 producer。

#### Scenario: Legacy BIOS 继续生成 v1 BootInfo

- **WHEN** Legacy BIOS bootloader 完成 kernel ELF64 加载
- **THEN** documented handoff address 处 MUST 继续存在可由现有 v1 ABI 校验的 `BootInfo`
- **AND** 现有 E820 metadata、boot drive、kernel load/entry metadata 和 kernel size metadata MUST 保持兼容

#### Scenario: Legacy BIOS 生成 v2 handoff blob

- **WHEN** Legacy BIOS bootloader 完成 kernel ELF64 加载并准备进入 kernel
- **THEN** 它 MUST 生成完整 v2 handoff blob，包含 boot protocol/core section 和由 BIOS E820 规范化而来的 memory map section
- **AND** v2 blob 的实际存放地址 MUST 避免覆盖现有 E820 buffer、legacy metadata aliases、v1 `BootInfo`、boot-stage page table 区域和 kernel load base

#### Scenario: Legacy BIOS 设置 register handoff

- **WHEN** Legacy BIOS bootloader 跳转到已校验的 kernel ELF entry
- **THEN** 它 MUST 设置 x86_64 第一个参数寄存器为 v2 `BootInfoHeader*`
- **AND** 它 MUST 保留 fixed low-address `BootInfo` 作为迁移期 fallback

#### Scenario: Legacy BIOS 不新增长期固件耦合

- **WHEN** Legacy BIOS backend 为统一 memory map consumer 提供输入
- **THEN** 它 MUST 通过 v2 memory map section 暴露规范化后的 E820 数据
- **AND** kernel memory、driver 或 IRQ 子系统 MUST NOT 新增对 BIOS interrupt、raw E820 fixed address 或 BIOS-only boot metadata 的长期依赖

### Requirement: Legacy boot-debug semantics remain unchanged

统一 boot handoff ABI SHALL NOT 改变现有 Legacy BIOS boot-debug 入口的用户语义。

#### Scenario: make boot-debug 仍代表 Legacy BIOS

- **WHEN** 开发者运行现有 `make boot-debug` 或等价 Legacy BIOS 调试入口
- **THEN** 构建和运行路径 MUST 继续使用现有 BIOS/MBR/exFAT/`boot.bin`/Bochs 语义
- **AND** 该入口 MUST NOT 隐式切换到 `BOOTX64.EFI`、ESP image 或 QEMU/OVMF UEFI 路径

#### Scenario: ABI 迁移不移动既有低地址布局

- **WHEN** register-passed v2 `BootInfoHeader*` 或 unified memory region view 被实现
- **THEN** 它 MUST NOT 移动现有 E820 buffer、legacy metadata aliases、v1 `BootInfo` address、boot-stage page table 区域或 kernel load base
- **AND** 如需新增固定地址或保留区，change MUST 更新地址布局文档并说明与未来 UEFI backend 的兼容关系
