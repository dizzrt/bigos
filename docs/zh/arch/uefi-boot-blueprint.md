# UEFI Boot Blueprint

本文档是 BigOS 的 UEFI 启动蓝图。BigOS 现在将 x86_64 UEFI boot backend 作为默认
可运行启动后端，runtime parity 边界限定为当前 bounded userland baseline。该后端
可以构建 `BOOTX64.EFI`、生成 ESP/FAT 镜像、提供 QEMU/OVMF 调试入口，并到达现有
resident init、`/bin/sh` 和有界 `/bin/*` payload 边界。Legacy BIOS 仍作为显式兼容与
低层调试 backend 保留。

## 当前范围

BigOS 默认启动路径现在是 UEFI：

```text
OVMF -> ESP/FAT -> EFI/BOOT/BOOTX64.EFI -> ELF64 kernel -> kernel(BootInfoHeader*) -> bounded userland
```

Legacy BIOS 路径显式保留，而不是被删除：

```text
Legacy BIOS path                         UEFI path
────────────────                         ─────────
MBR -> DBR -> exDBR -> boot.bin          BOOTX64.EFI
              │                              │
              ├─ BIOS E820                  ├─ UEFI GetMemoryMap
              ├─ VGA text                   ├─ firmware console/serial
              ├─ ATA/exFAT                  ├─ SimpleFileSystem/ESP
              │                              │
              └──────── normalize ──────────┘
                           │
                      BootInfo v2+
                           │
                        kernel()
```

默认 UEFI backend 的非目标：

- 不改变 `xmake run bochs` 及其 `--display sdl2|none` target arguments 的 Legacy BIOS/MBR/exFAT/Bochs 语义。
- 不替换 MBR、DBR、extended DBR、`boot.bin` 或现有 raw exFAT image。
- 不宣称超出当前 bounded userland baseline 的 runtime parity。
- 不实现 Secure Boot、ACPI table handoff、UEFI Runtime Services、持久 NVRAM 语义、新 SMP 范围或第二 ISA。
- 不把 GOP framebuffer metadata handoff 等同于 glyph rendering、Unicode display、framebuffer scrollback 或图形 console parity。
- 不要求 kernel 调用 BIOS interrupt、UEFI Boot Services 或 UEFI Runtime Services。
- 不引入外部 UEFI 库、hosted runtime、异常、RTTI 或其它非 freestanding 依赖。

## Kernel Entry 假设

显式 BIOS backend 与默认 UEFI backend 进入 kernel 前都必须提供统一、可验证的入口环境：

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
- UEFI backend 生产 v2 blob，包含 required `core` 与 `memory_map` sections，
  以及 optional `storage_metadata` 与 `loader_metadata` sections。
- UEFI backend 也可以生产 optional `framebuffer_metadata` 与 `font_asset_metadata`
  sections。framebuffer section 从固件当前 GOP 模式规范化而来，记录物理基址、字节大小、宽高、
  pixels per scanline、pixel format、bytes/bits per pixel 以及写入/cache hint。
  font section 描述 ESP 加载的首个 boot font asset。
- runtime `_start` 保存入口 `BootInfoHeader*`，调用 `_init` 后恢复为 `kernel()` 的第一个参数。
- kernel consumer 校验 magic、version、size、alignment、field offset、section offset、section size 和边界。
- 未识别的非必需 section 可以跳过；必需 section 缺失或格式错误导致 v2 失败，并显式 fallback 到 v1 fixed-address `BootInfo`。
- BIOS backend 与 UEFI backend 都作为统一 handoff producer，不允许 kernel consumer 直接依赖固件原始协议。

仍未实现的范围：

- 超出当前 bounded userland baseline 的 UEFI runtime parity。
- Glyph rendering、Unicode display、framebuffer scrollback、ACPI/SMBIOS firmware table section 和 UEFI Runtime Services 支持。
- Secure Boot、持久 NVRAM 语义和非 x86_64 ISA backend。

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

当存在有效 framebuffer metadata section 时，即使固件也把重叠区域报告为 `usable`，framebuffer
物理范围仍按保守策略处理：early buddy 初始化会从 ordinary RAM free pool 中排除该子范围，
direct-map 初始化也会跳过它，避免后续 framebuffer writer 依赖 ordinary-RAM alias。kernel 代码如果需要写 firmware framebuffer，必须通过
`bigos::mm::map_device_mmio(physical_base, length, cache_policy)` 获取虚拟地址；直接通过
`phys_to_direct()` 写 framebuffer 不属于允许边界。

UEFI `GetMemoryMap` 初步映射方向：

- `EfiConventionalMemory` 映射为 `usable`。
- `EfiLoaderCode`、`EfiLoaderData`、`EfiBootServicesCode` 和 `EfiBootServicesData`
  映射为 `loader`，不会进入初始 free page pool。
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

`xmake run qemu` 和 `uv run python tools/boot_debug.py run` 是默认 UEFI/QEMU/OVMF 启动入口。`xmake run qemu-uefi` 仍是同一后端的显式别名。`xmake run qemu-legacy`、`xmake run qemu-gdb` 和带 `--display sdl2|none` 的 `xmake run bochs` 是显式 Legacy BIOS/MBR/exFAT 调试路径：

- 构建 MBR、DBR、extended DBR、`boot.bin` 和 root `kernel`。
- 生成 raw exFAT disk image。
- 使用 QEMU IDE disk 路径进行显式 Legacy 本地启动、headless serial-marker smoke 或 GDB stub 调试。
- 保留 Bochs 作为支持的 Legacy BIOS 本地调试入口，用于早期 boot 和硬件行为交叉验证。
- 不要求 UEFI loader、ESP image、OVMF 配置、Secure Boot、GOP framebuffer、ACPI handoff、Runtime Services 或新存储驱动。

默认 UEFI backend 使用与 Legacy BIOS 分离的产物：

- `xmake build uefi-artifacts` 构建 `build/bin/x86/uefi/BOOTX64.EFI`。
- 首个 boot-time font 源资产固定为 `assets/fonts/unifont_all-17.0.04.hex`。Python image
  helper 将随附 Unifont HEX 数据转换为版本化 glyph lookup payload
  `build/assets/fonts/unifont.bin`，并打包到 ESP 的 `/boot/fonts/unifont.bin`；UEFI loader
  只消费 ESP runtime path，并且只做基本格式门禁。
- `xmake run qemu -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  会准备 `build/test/uefi-esp.img`，并准备 `build/test/uefi-root.raw` 作为当前 exFAT runtime root 兼容镜像，复制可写 OVMF vars 文件到
  `build/test/OVMF_VARS.uefi.fd`，并启动 QEMU/OVMF。
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  是直接 helper 形式。

UEFI 产物隔离策略：

- BIOS 路径继续使用 raw exFAT image 和 `build/test/os.raw` 一类产物。
- UEFI 路径使用 ESP/FAT image，包含 `EFI/BOOT/BOOTX64.EFI`、`/boot/kernel`、
  `/boot/user/init.elf` 和有界 `/bin/*` payload。在 FAT runtime filesystem 或 loader-fed runtime payload
  落地前，QEMU 还会将独立 exFAT 兼容 root image 挂为 primary IDE，使当前 kernel VFS 可以到达同一有界用户态基线。
- ESP/FAT image 同时包含 `/boot/fonts/unifont.bin`，供 framebuffer handoff metadata 和后续
  kernel glyph lookup view 使用。font metadata 缺失、非法或 lookup 校验失败都是文档化 fallback，
  不能阻塞 serial diagnostics、VGA text fallback、memory initialization 或 bounded userland validation。
- UEFI 固件配置、临时目录和 emulator 配置不得覆盖 `xmake run qemu`、`xmake run qemu-gdb` 或 `xmake run bochs` 使用的 Legacy BIOS 产物。
- UEFI smoke test 首选 QEMU + OVMF；该后端不要求 Bochs UEFI。
- Legacy BIOS 继续使用当前 raw exFAT image，并通过 QEMU IDE 或 Bochs 启动。

UEFI 本地工具假设：

- QEMU 配套 x86_64 OVMF code firmware 和 vars template；存在 Homebrew QEMU 路径时，
  会自动探测 `edk2-x86_64-code.fd` 和 `edk2-i386-vars.fd` 等文件。
- Homebrew LLVM/LLD 工具：`clang`、`lld-link`、`llvm-objcopy` 和 `llvm-objdump`。
- `mtools` 命令：`mformat`、`mmd`、`mcopy` 和 `mdir`。
- 现有 x86_64 cross toolchain 仍用于构建 kernel 和用户态 payload。
- Apple Silicon 主机可能通过 TCG 运行 x86_64 QEMU，速度较慢，smoke timeout 可能需要更长。

UEFI BootInfo metadata sections：

- UEFI `core` section 中 `boot_protocol` 为 `UEFI`，`exfat_data_area_lba` 为零；
  ESP 或 root storage 身份不会编码到 Legacy exFAT 字段。
- Optional `storage_metadata` section 描述 UEFI ESP/root 来源和 boot path。
- Optional `loader_metadata` section 记录诊断用 backend、loader version/build id、
  firmware vendor/revision 和 boot file path 信息。
- Optional `framebuffer_metadata` section 在 `ExitBootServices` 前记录 UEFI GOP 当前模式的
  geometry 与物理 framebuffer 边界。kernel 将其解析为 immutable optional view；metadata 缺失时
  Legacy/VGA text 与 serial fallback 仍然有效，metadata 非法时在任何 framebuffer 写入前忽略。运行期 framebuffer console 渲染只能在通过
  `bigos::mm::map_device_mmio()` 映射该范围后消费这个 view。
- Optional `font_asset_metadata` section 记录 ESP 加载的 `/boot/fonts/unifont.bin` buffer 地址、
  字节大小、glyph lookup format version、glyph/cell metrics 和 loader-provided flags。UEFI loader
  校验 glyph lookup header magic、header size、declared byte size、format version、table offsets
  和基本 metrics，但不解析 Unicode ranges、不搜索 glyph records、不分类 terminal cells，也不写 framebuffer
  pixels。kernel startup 会在暴露只读 lookup view 前校验 payload header、range table、glyph records、
  bitmap bounds、alignment 和 width classes，供后续 console code 使用。
- Glyph lookup asset 当前记录 Unifont 8x16 半宽和 16x16 全宽 bitmap glyph。runtime console
  会把普通输出按有界 UTF-8 解码为 console-owned Unicode codepoint cell，并使用该 width class
  布局半宽和全宽 glyph。framebuffer backend 可以用软件光标渲染这些 cell；Legacy VGA text backend
  会对非 ASCII cell 做确定性降级。这仍是有界默认 console 能力，不会把默认 console 扩大为完整图形、
  ANSI/VT、locale-aware、shaping 或 POSIX terminal。
- Kernel 启动仍只依赖有效的 required `core` 与 `memory_map` sections；缺失或未知的
  optional section 仍可跳过。

## ELF64 加载规则

UEFI loader 单独实现适合 UEFI 的 ELF reader，不直接复用当前 `boot.cc` 中混合 ATA、
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
| 2 | 内存模块迁移到统一 `BootMemoryRegion` consumer，并保留 BIOS fallback | 是，BIOS E820 已规范化 | unified boot handoff capability header 和 memory map section 草案 | allocator 初始化顺序、可用内存误判 | `define-unified-boot-handoff-abi` |
| 3 | 最小 UEFI loader，单独实现 UEFI ELF reader，目标仅为加载 kernel、填充 handoff、进入 `kernel()` | 是，已晋升为默认 UEFI backend 的组成部分 | unified boot handoff capability ABI、ELF64 加载规则、工具链 spike | PE/COFF 构建、ExitBootServices 顺序、页表差异 | `spike-minimal-uefi-loader` |
| 4 | ESP/FAT 镜像生成、OVMF/QEMU 调试入口和文档化命令 | 是，已晋升为默认 UEFI backend 的组成部分 | kernel memory API capability loader 可启动 | 宿主机 OVMF 路径、CI 可移植性、产物隔离 | `add-uefi-boot-debug-entry` |
| 5 | GOP framebuffer metadata handoff、ACPI RSDP/SMBIOS handoff 和更完整的 UEFI 验证策略 | 部分：仅完成 GOP framebuffer/font metadata handoff | unified boot handoff capability sections、kernel memory API capability/UEFI smoke test | framebuffer 映射、ACPI 表生命周期、runtime metadata 误用 | `handoff-gop-acpi-firmware-tables` |
| 6 | BIOS 与 UEFI 共享 ELF64 加载规则规范，但不要求近期共享 loader 代码 | 否 | 当前 BIOS ELF 加载行为文档化 | 规则与实现漂移、错误处理不一致 | `document-common-elf64-loader-rules` |

UEFI 默认 runtime parity 仅限当前 resident init、shell 和 packaged user-program 基线。后续 firmware parity
工作必须保持 Legacy BIOS 路径可被显式选择，并在需要 BIOS/ATA/port-IO 行为验证时继续使用
`xmake run qemu-legacy`、`xmake run qemu-gdb` 或 `xmake run bochs`。
