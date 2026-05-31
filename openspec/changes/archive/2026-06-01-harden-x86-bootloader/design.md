## 上下文

BigOS 当前使用 legacy x86 BIOS 启动链：

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> higher-half ELF64 kernel
```

这条启动链短小且显式，但依赖若干隐式契约：

- MBR 分区表项由 `tools/install.py` 直接从磁盘镜像读取。
- BIOS boot drive 初始位于 `DL`，被保存到 `0x802`，但后续阶段复用并不一致。
- 扩展 DBR 将 E820 和其他启动数据存放在固定低地址。
- `boot.cc` 假设 exFAT 目录布局简单，且 ELF 只有一个 load segment。
- `Makefile` 会生成多个二进制镜像，但只有部分镜像有实际安装行为。

本 change 影响 x86 boot 子系统和早期内存管理 handoff。它不引入新的标准启动协议，而是让当前自定义路径更显式、更可验证、更安全。

## 目标 / 非目标

**目标：**

- 让 MBR、DBR、扩展 DBR 和 `boot.bin` 的磁盘镜像安装变得确定。
- 修复分区扫描、exFAT 文件属性判断和 boot-drive 传递中的正确性问题。
- 改善 loader 失败行为，让缺失文件和读盘失败以可诊断错误停止，而不是继续落入未定义路径。
- 支持包含多个 `PT_LOAD` segment 的 ELF64 内核，并正确处理 zero-filled memory。
- 定义 `BootInfo` handoff 契约，替代零散低地址值，同时保留当前 kernel 所需信息。
- 为 boot sector 和预留区域约束增加构建期二进制大小检查。

**非目标：**

- 不迁移到 UEFI、Multiboot2、Stivale 或 Limine。
- 不引入 hosted 文件系统层，也不实现完整 exFAT。
- 不新增 AHCI、NVMe、USB、virtio 或通用块设备栈。
- 除非验证明确需要，否则不改变 higher-half kernel base。
- 不涉及调度器、进程模型、用户态或文件系统服务。

## 设计决策

### 保留 Legacy BIOS 启动链

实现上将加固现有 MBR/exFAT/ELF 路径，而不是替换为标准 bootloader。

理由：

- 当前仓库处于早期内核 bring-up 阶段，保留现有路径可以让变更聚焦。
- 现有代码已经覆盖实模式、保护模式、长模式、BIOS 读盘、ATA PIO 和 ELF 加载等有价值的低层概念。

备选方案：

- 使用 Limine 或 Multiboot2。这样能减少 bootloader 维护成本，但会偏离当前学习型自研启动实现，并显著扩大迁移范围。

### 使用带版本的 BootInfo 契约

在低内存中引入紧凑的 `BootInfo` 结构，并让 kernel consumer 通过命名字段读取。该结构至少包含：

- magic value
- version
- size
- boot drive
- E820 entry count
- E820 entry address
- exFAT data-area LBA
- kernel load virtual address
- kernel memory size
- 可用时的 kernel entry address
- optional fields flags

迁移期间可以保留当前低地址作为兼容别名，但目标 handoff 应收敛到 `BootInfo`。

canonical `BootInfo` ABI 定义放在一个极小的 x86 boot handoff 公共头中，例如
`include/arch/x86/boot/boot_info.h` 或等价路径。该头只暴露 magic、version、
handoff address、字段 offset 常量和 C-compatible packed struct，不依赖 kernel 子系统
或 C++ runtime。assembly 使用同源宏或受校验的 `.equ` 常量，boot C++ 和 kernel C++
通过 `static_assert` 校验 struct size、alignment 和关键字段 offset。

理由：

- 当前内存初始化依赖 `0x500`、`0x800`、`0x80c` 等魔法地址。
- 带版本结构能让未来扩展更安全，避免静默改变 ABI 假设。
- 将 ABI 定义放在公共 x86 handoff 头中，可以避免 bootloader 或 kernel 任一侧成为
  隐式权威来源。

备选方案：

- 继续保留魔法地址，仅补文档。该方案侵入性更低，但无法防止未来 producer/consumer 意外不匹配。
- 只在 `boot.cc` 或 `src/mm/buddy.cc` 附近定义结构。该方案局部简单，但 assembly、boot C++
  和 kernel C++ 很容易复制出不一致的 offset。

### 保留固定早期地址布局并补充文档

本 change 会文档化并校验当前早期地址图，而不是重新设计布局。

预期布局包括：

```text
0x0500..      E820 ARDS records
0x0800..      early boot metadata / BootInfo compatibility area
0x1000..      extended DBR load address
0x2000..      early PML4 and page-table area
0x5000..      kernel page directory/table handoff area
0x7c00        BIOS-loaded MBR/DBR address
0x0f000       boot directory buffer
0x10000       boot.bin load address
0x100000      kernel page tables / mapped physical start assumptions
0xffffffff80000000 higher-half kernel virtual base
```

理由：

- 修改早期地址可能破坏 bootability 和页表假设。
- 文档加大小检查能在不大改内存布局的情况下提供即时安全性。

### 实现最小 exFAT 感知安装

`tools/install.py` 将负责把生成的 `boot.bin` 写入受支持的 exFAT 磁盘镜像布局；如果所需目录/文件放置无法安全更新，则以明确诊断失败。该 change 只支持已有、连续、容量足够的 `/boot/boot.bin`；`tools/install.py` 不负责新建文件、扩展文件、分配 cluster、更新 allocation bitmap，或生成新的 exFAT 目录项。

必要行为：

- 读取全部四个分区表项。
- 定位受支持的 exFAT 分区。
- 使用 exFAT boot-sector 元数据定位 data area、root directory 和 `/boot/boot.bin` entry。
- 仅当目标文件已预分配、连续且容量能够容纳 `boot.bin` 时写入。
- 对缺失文件、非连续分配、容量不足或目录布局不受支持的镜像，以明确 unsupported-layout 或等价错误失败。
- 不再静默宣称 `boot.bin` 已安装。

理由：

- `Makefile` 已调用 `install.py --with-boot`，该命令必须安装成功或明确失败。
- 完整 exFAT 写入器会显著扩大范围，并增加误更新 FAT、bitmap 或目录项导致镜像损坏的风险。

备选方案：

- 要求用户手动挂载镜像并复制 `boot.bin`。这更容易实现，但会让构建/安装路径不完整且不可重复。
- 让 `tools/install.py` 创建或扩展 `/boot/boot.bin`。这能降低镜像准备成本，但等价于实现更多
  exFAT 写路径，不符合“不实现完整 exFAT”的非目标。

### 为磁盘读取增加超时和错误码

BIOS 和 ATA 读循环都应在明确失败条件下停止：

- BIOS `int 13h` 读取检查 carry flag 和 status。
- ATA PIO 等待检查 `BSY`、`DRQ`、`ERR` 和 `DF`。
- 等待循环使用有界重试次数。
- 失败路径打印简短的阶段级错误并 halt。

理由：

- 启动期间无限循环在 Bochs 和真实硬件上都难以诊断。
- 错误码有助于区分缺失文件和控制器失败。

### 按 Program Header 加载 ELF64

`boot.cc` 将解析 ELF header 和 program header table，然后加载每个 `PT_LOAD` segment。

必要行为：

- 校验 ELF magic、class、endianness、machine 和 header size。
- 读取足够扇区以覆盖 program header table。
- 对每个 `PT_LOAD`，按 `p_offset` 读取 `p_filesz` 字节到 `p_vaddr` 或受支持的映射地址。
- 对 `p_memsz - p_filesz` 做 zero-fill。
- 跟踪 kernel 总内存占用，用于早期内存初始化。
- 记录并校验 ELF `e_entry`，确保它落在已加载的 `PT_LOAD` 虚拟地址范围内。
- 进入 kernel 时优先跳转到已校验的 ELF entry point，而不是继续假设入口固定等于 higher-half base。

理由：

- 当前 linker script 生成单个 `PT_LOAD`，但长期依赖这个假设很脆弱。
- 在 kernel layout 扩大前，应先实现正确 ELF 语义。
- ELF `e_entry` 是链接器声明的入口，使用它可以避免 section 顺序或入口符号位置变化时
  继续依赖 `0xffffffff80000000` 这个隐式假设。

备选方案：

- 继续跳转固定 higher-half base。该方案最接近当前行为，但会把 `_start` 必须位于
  higher-half 起始地址的布局假设永久固化。

## 风险 / 权衡

- [风险] 引入 `BootInfo` 时 producer 和 consumer 布局不一致会破坏 `src/mm/buddy.cc` -> [缓解] 在 kernel consumer 迁移并验证前保留现有魔法地址兼容写入。
- [风险] 写入 exFAT 中的 `boot.bin` 时，如果误解 FAT 分配，可能损坏镜像 -> [缓解] 初期只支持连续预分配目标空间，校验容量，并 fail closed。
- [风险] ELF 多段加载可能因畸形 segment 覆盖页表或 bootloader 内存 -> [缓解] 读入前校验目标范围不与文档化保留区域冲突。
- [风险] 超时常量对某些模拟器或磁盘过低 -> [缓解] 使用保守重试次数，并将常量命名以便调参。
- [风险] 更多检查会增加 boot-sector 代码大小 -> [缓解] MBR/DBR 保持最小检查，较大逻辑放入扩展 DBR 或 `boot.bin`，并强制二进制大小检查。

## 迁移计划

1. 修复 installer 分区扫描，并在不改变启动行为的前提下增加二进制大小检查。
2. 为受支持 exFAT 布局实现显式 `boot.bin` 安装或 fail-closed 校验。
3. 修复 boot-drive 传递和 exFAT 文件属性判断。
4. 增加有界读盘等待和可见失败路径。
5. 在保留现有低地址兼容字段的同时生成 `BootInfo`。
6. 更新 kernel consumer，使其优先读取 `BootInfo`，仅在迁移阶段回退到兼容路径。
7. 将单 program header ELF 加载替换为多 `PT_LOAD` 加载。
8. 使用最小必要构建检查验证；本地 Bochs 镜像和工具链配置可用时，执行 emulator smoke test。

回滚策略：

- 每个步骤都应保留上一版二进制布局假设的可见性。
- 如果出现启动回归，回滚最近一步，同时尽量保留诊断和大小检查。

## 已确认决策

- `boot.bin` 安装要求镜像中已有预分配、连续、容量足够的 `/boot/boot.bin`；`tools/install.py`
  只做受支持布局的定位、校验和覆盖写入。
- canonical `BootInfo` ABI 定义放在公共 x86 boot handoff 头中，并用 offset 常量和
  `static_assert` 防止 assembly、boot C++ 与 kernel C++ 布局漂移。
- bootloader 最终跳转到已校验的 ELF entry point；固定 higher-half base 仅作为当前加载和映射
  布局约束，不作为长期入口规则。
