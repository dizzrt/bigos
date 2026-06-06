# UEFI Boot Blueprint

本文档是 BigOS 的 UEFI 启动蓝图。当前阶段只建立设计约束和项目级路线图，
不实现 `BOOTX64.EFI`，不生成 ESP 镜像，不新增 UEFI emulator smoke test，
也不改变现有 Legacy BIOS 启动路径。

## 当前范围

BigOS 当前可运行的启动路径仍然是 Legacy BIOS：

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel -> kernel(BootInfoHeader*)
```

未来 UEFI 路径应作为并行 boot backend 引入，而不是替换当前路径：

```text
Legacy BIOS path                         UEFI path
────────────────                         ─────────
MBR -> DBR -> exDBR -> boot.bin          BOOTX64.EFI
              │                              │
              ├─ BIOS E820                  ├─ UEFI GetMemoryMap
              ├─ VGA text                   ├─ GOP framebuffer
              ├─ ATA/exFAT                  ├─ SimpleFileSystem/ESP
              │                              │
              └──────── normalize ──────────┘
                           │
                      BootInfo v2+
                           │
                        kernel()
```

本阶段的非目标：

- 不实现 `BOOTX64.EFI`。
- 不改变 `make boot-debug` 的 Legacy BIOS/MBR/exFAT/Bochs 语义。
- 不替换 MBR、DBR、extended DBR、`boot.bin` 或现有 raw exFAT image。
- 不实现 `BOOTX64.EFI`、ESP/FAT UEFI image、QEMU/OVMF UEFI 入口或 UEFI Runtime Services。
- 不要求 kernel 调用 BIOS interrupt、UEFI Boot Services 或 UEFI Runtime Services。
- 不引入外部 UEFI 库、hosted runtime、异常、RTTI 或其它非 freestanding 依赖。

## Kernel Entry 假设

BIOS backend 与未来 UEFI backend 进入 kernel 前都必须提供统一、可验证的入口环境：

- 架构目标是 x86_64。
- kernel 保持 higher-half ELF64 executable。
- CPU 已进入 long mode。
- 分页已开启，并包含 kernel higher-half 映射和必要的早期物理/identity 映射。
- 栈可用，并满足调用约定要求的对齐。
- 中断保持关闭，IDT 和中断控制器状态由 kernel 初始化流程接管。
- `BootInfo` 或其后续版本在进入 kernel 时可用。
- 长期 kernel entry ABI 通过约定寄存器传递 `BootInfo*`，例如 x86_64 System V 风格使用 `rdi`。
- 固定低地址 handoff 仅作为 Legacy BIOS 迁移期 fallback 或调试兼容手段。

kernel consumer 不应直接消费 BIOS E820、UEFI raw descriptor、UEFI system table 或其它固件原始协议。
boot backend 负责把固件数据规范化为统一 handoff。

## BIOS 地址布局兼容

现有 Legacy BIOS 固定地址布局在本蓝图阶段保持不变。当前地址约束记录在
`docs/zh/arch/x86-boot-layout.md`，关键 handoff 区域包括：

```text
0x0500..0x07ff  E820 ARDS records written by extended DBR
0x0800..0x083f  legacy boot metadata aliases
0x0840..0x0887  canonical BootInfo handoff structure
0x2000..0x6fff  boot-stage PML4/PDPT/PD/PT setup area
0x5000..        kernel higher-half page-directory handoff area
0x9000..0x9fff  Legacy BIOS-produced BootInfo v2 handoff blob
0x100000        kernel higher-half page-table backing area
0x1000000       kernel physical load base
0xffffffff80000000  kernel higher-half virtual base
```

后续任何 handoff ABI 或地址布局变更都必须同步评审：

- `BIGOS_BOOT_INFO_ADDRESS` 与 legacy alias 的兼容关系。
- E820 buffer、boot metadata alias、page-table reservation 和 kernel load base。
- `link.lds` 中 higher-half base 与 ELF segment 加载假设。
- `boot.s` 到 `boot.cc`、`boot.cc` 到 kernel entry 的寄存器和栈约定。
- `BootInfo` magic、version、size、field offset 和 alignment 校验。
- Legacy BIOS fallback 是否仍能生成与 kernel consumer 匹配的数据。

## BootInfo 与 Handoff 规划

`BootInfo` v2 ABI 基础已经落地为 `BootInfoHeader + tagged sections`。固定 v1 struct
仍保留在 `BIGOS_BOOT_INFO_ADDRESS`，仅作为 Legacy BIOS fallback；v2 使用独立 magic，
通过 `rdi` 传递 `BootInfoHeader*`，并用相对 header 的 section offset/size 描述
payload。

建议的长期结构：

```text
BootInfoHeader
  magic
  version
  header_size
  total_size
  flags
  boot_protocol
  section_count
  section_table_offset

BootInfoSection[]
  type
  flags
  offset
  size
  alignment
```

需要覆盖的 section 类别：

- boot protocol：标识 Legacy BIOS、UEFI 或其它 backend，以及 loader capabilities。
- unified memory map：标准化后的 `BootMemoryRegion[]`。
- framebuffer：GOP 或其它显示 backend 暴露的 framebuffer 信息。
- firmware tables：ACPI RSDP、SMBIOS entry point 等固件表入口。
- loader metadata：loader 名称、版本、镜像布局、调试标志和保留区。
- ABI compatibility：旧版 `BootInfo` fallback、必需 section、可选 section 和 unknown section 跳过策略。

当前已实现的基础：

- Legacy BIOS backend 生产完整 v2 blob，包含 required `core` section 和 required
  `memory_map` section。
- runtime `_start` 保存入口 `BootInfoHeader*`，调用 `_init` 后恢复为 `kernel()` 的第一个参数。
- kernel consumer 校验 magic、version、size、alignment、field offset、section offset、section size 和边界。
- 未识别的非必需 section 可以跳过；必需 section 缺失或格式错误导致 v2 失败，并显式 fallback 到 v1 fixed-address `BootInfo`。
- BIOS backend 与未来 UEFI backend 都必须作为统一 handoff producer，不允许 kernel consumer 直接依赖固件原始协议。

仍未实现的范围：

- `BOOTX64.EFI` loader、ESP/FAT image 和 QEMU/OVMF UEFI 调试入口。
- GOP framebuffer、ACPI/SMBIOS firmware table section 和 loader metadata section。
- UEFI `GetMemoryMap` producer、`ExitBootServices()` 处理和 Runtime Services 支持。

## 统一内存图规划

内存模块已经从 primary raw E820 consumer 迁移到统一 `BootMemoryRegion` consumer。
Legacy BIOS fallback 仍可从 v1 `BootInfo` 指向的 E820 ARDS 栈上临时规范化。
`BootMemoryRegion` 至少表达：

```text
physical_base
length
normalized_type
attributes
source_type
```

初步 normalized type：

- `usable`
- `reserved`
- `acpi_reclaim`
- `acpi_nvs`
- `mmio`
- `loader`
- `kernel`
- `bad_memory`
- `runtime`

BIOS E820 映射：

- E820 type 1 映射为 `usable`。
- E820 type 2 映射为 `reserved`。
- E820 type 3 映射为 `acpi_reclaim`。
- E820 type 4 映射为 `acpi_nvs`。
- E820 type 5 映射为 `bad_memory`。
- 未识别类型保守映射为 `reserved`，并保留原始类型到 attributes/source metadata。

early buddy 初始化只释放 `usable` 区域。`reserved`、`runtime`、`mmio`、
`acpi_reclaim`、`acpi_nvs`、`bad_memory` 和 unknown 区域都不会进入 buddy free list。
`acpi_reclaim` 在 ACPI 表发现、复制和生命周期管理完成前保持保留。

UEFI `GetMemoryMap` 初步映射方向：

- `EfiConventionalMemory` 映射为 `usable`。
- `EfiLoaderCode`、`EfiLoaderData` 可映射为 `loader` 或在退出 boot services 后按保留策略处理。
- `EfiBootServicesCode`、`EfiBootServicesData` 需要由 UEFI loader 在 `ExitBootServices()` 后按策略规范化。
- `EfiACPIReclaimMemory` 映射为 `acpi_reclaim`。
- `EfiACPIMemoryNVS` 映射为 `acpi_nvs`。
- `EfiMemoryMappedIO`、`EfiMemoryMappedIOPortSpace` 映射为 `mmio`。
- `EfiRuntimeServicesCode`、`EfiRuntimeServicesData` 映射为 `runtime`，并保留 `EFI_MEMORY_RUNTIME`。
- 未识别或固件保留类型保守映射为 `reserved`。

近期不支持调用 UEFI Runtime Services。即使如此，统一 memory map 仍必须保留 runtime memory
类型和 attributes，包括 `EFI_MEMORY_RUNTIME`、cacheability、write-back/write-combine 等信息，
避免未来支持 UEFI variables、reset、time 或 firmware diagnostics 时返工。

后续独立 change 候选：

- `define-bootinfo-v2-handoff`：定义 `BootInfoHeader`、section table 和 register handoff ABI。
- `migrate-mm-to-boot-memory-map`：将内存模块迁移到统一 `BootMemoryRegion` consumer，并保留 BIOS fallback。
- `define-firmware-memory-map-normalization`：细化 E820 与 UEFI descriptor 到统一内存图的映射和验证。

## 调试入口与镜像规划

`make boot-debug` 继续代表 Legacy BIOS/MBR/exFAT/Bochs 调试路径：

- 构建 MBR、DBR、extended DBR、`boot.bin` 和 root `kernel`。
- 生成 raw exFAT disk image。
- 使用 Bochs 作为 Legacy BIOS 本地调试入口。
- 不隐式切换为 UEFI loader、ESP image 或 OVMF 配置。

未来 UEFI 调试入口必须使用独立命名和独立产物，例如：

- `make uefi-boot-debug`
- `python3 tools/uefi_boot_debug.py run`

未来 UEFI 产物隔离策略：

- BIOS 路径继续使用 raw exFAT image 和 `build/test/os.raw` 一类产物。
- UEFI 路径使用 ESP/FAT image，包含 `EFI/BOOT/BOOTX64.EFI` 和 kernel ELF。
- UEFI 固件配置、临时目录和 emulator 配置不得覆盖 `make boot-debug` 的 Bochs 产物。
- UEFI smoke test 正式首选 QEMU + OVMF；Bochs UEFI 仅作为可选验证路径。
- Legacy BIOS 继续使用 Bochs。

## ELF64 加载规则

未来 UEFI loader 单独实现适合 UEFI 的 ELF reader，不直接复用当前 `boot.cc` 中混合 ATA、
exFAT、固定低地址和页表准备的实现。BIOS 与 UEFI loader 需要共享同一套 ELF64 加载规则规范：

- 只支持 ELF64 x86_64 executable kernel。
- 校验 ELF header、program header table 边界、`PT_LOAD` segment 文件范围和内存范围。
- 按 `PT_LOAD` segment 加载到预期虚拟/物理映射目标。
- 对 `p_memsz > p_filesz` 的 segment 执行 zero-fill。
- 校验 `e_entry` 落在已加载 segment 内。
- 拒绝不支持或越界的 ELF，而不是尝试容错执行。

## 项目级路线图

| 阶段 | 规划项 | 当前是否实现 | 推荐前置条件 | 主要风险 | OpenSpec change 候选 |
| --- | --- | --- | --- | --- | --- |
| 1 | `BootInfoHeader + tagged sections`、寄存器传递 `BootInfo*`、统一 handoff header 设计与文档化 | 是，Legacy BIOS producer/consumer 已落地 | 当前 BIOS `BootInfo` layout 校验稳定 | ABI 破坏、legacy fallback 不一致 | `define-unified-boot-handoff-abi` |
| 2 | 内存模块迁移到统一 `BootMemoryRegion` consumer，并保留 BIOS fallback | 是，BIOS E820 已规范化 | 阶段 1 header 和 memory map section 草案 | allocator 初始化顺序、可用内存误判 | `define-unified-boot-handoff-abi` |
| 3 | 最小 UEFI loader spike，单独实现 UEFI ELF reader，目标仅为加载 kernel、填充 handoff、进入 `kernel()` | 否 | 阶段 1 ABI、ELF64 加载规则、工具链 spike | PE/COFF 构建、ExitBootServices 顺序、页表差异 | `spike-minimal-uefi-loader` |
| 4 | ESP/FAT 镜像生成、OVMF/QEMU 调试入口和文档化命令 | 否 | 阶段 3 loader 可启动 | 宿主机 OVMF 路径、CI 可移植性、产物隔离 | `add-uefi-boot-debug-entry` |
| 5 | GOP framebuffer、ACPI RSDP/SMBIOS handoff 和更完整的 UEFI 验证策略 | 否 | 阶段 1 sections、阶段 3/4 UEFI smoke test | framebuffer 映射、ACPI 表生命周期、runtime metadata 误用 | `handoff-gop-acpi-firmware-tables` |
| 6 | BIOS 与 UEFI 共享 ELF64 加载规则规范，但不要求近期共享 loader 代码 | 否 | 当前 BIOS ELF 加载行为文档化 | 规则与实现漂移、错误处理不一致 | `document-common-elf64-loader-rules` |

每个阶段都是后续 change 候选项，不属于本 change 的运行时代码实现任务。后续实现必须保持
Legacy BIOS 路径可回退，并用 `make boot-debug` 继续验证现有调试入口。
