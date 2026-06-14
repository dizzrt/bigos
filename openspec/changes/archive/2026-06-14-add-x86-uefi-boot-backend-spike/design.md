## Context

BigOS 当前可运行 backend 是 x86_64 Legacy BIOS/MBR/exFAT：boot sector 链路加载 `boot.bin`，`boot.bin` 读取 exFAT 中的 kernel ELF，建立进入 higher-half kernel 所需的早期状态，并通过 `BootInfo v2` 向 kernel 传递启动数据。`uefi-boot-blueprint` 已经规划了 UEFI 方向，但明确不要求实现 `BOOTX64.EFI`、ESP 镜像或 OVMF smoke。

本 change 将 roadmap 中 backend 扩展试探的低风险路径具体化为 x86_64 UEFI boot backend spike。它不改变 ISA，不替换现有 Legacy BIOS backend，而是新增一个并行的 UEFI loader、ESP/FAT 镜像生成和 QEMU/OVMF 启动入口，用于验证前序架构边界是否足够支撑第二个 boot backend。

数据流：

```text
QEMU + OVMF
    |
    v
ESP/FAT: EFI/BOOT/BOOTX64.EFI
    |
    |-- LocateProtocol(SimpleFileSystem)
    |-- 读取 /boot/kernel 和用户态 payload
    |-- 解析 ELF64 program headers
    |-- 分配/加载 kernel segments
    |-- GetMemoryMap -> BootMemoryRegion[]
    |-- 构造 BootInfo v2 + storage/loader metadata sections
    |-- ExitBootServices
    v
x86_64 kernel entry(BootInfoHeader*)
    |
    v
现有 kernel() 初始化路径
```

## Goals / Non-Goals

**Goals:**

- 新增独立 x86_64 UEFI loader spike，产出 `BOOTX64.EFI` 并从 QEMU + OVMF 启动。
- 使用现有 `BootInfo v2` 作为 primary kernel handoff ABI，UEFI loader 在进入 kernel 前规范化 UEFI memory map。
- 新增独立 ESP/FAT 镜像生成路径，打包 kernel、PID-1 init 和 `/bin/*` 用户态程序。
- 新增独立 QEMU/OVMF 调试入口和 headless 串口 marker 验证路径，目标达到默认 PID-1 init 与 `/bin/sh` 的 Legacy 等价 marker。
- 保留现有 Legacy BIOS/MBR/exFAT QEMU/Bochs 路径、产物、默认行为和验证语义。

**Non-Goals:**

- 不新增第二 ISA，不引入 AArch64、RISC-V 或跨 ISA runtime parity。
- 不把 UEFI backend 设为默认启动路径，不删除或重写现有 BIOS boot sectors。
- 不实现 Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services、NVRAM 持久语义或图形控制台。
- 不引入 SMP、动态链接、完整 POSIX、完整 libc、广泛 file-backed `mmap`、持久完整可写文件系统或新存储驱动。
- 不 vendor 完整 edk2；spike 阶段使用本机 QEMU edk2 固件和轻量 UEFI ABI/header/glue。

## Decisions

### Decision: 新增并行 UEFI backend，而不是迁移现有 BIOS backend

UEFI 入口使用独立构建目标、独立镜像和独立 run target。现有 BIOS 目标继续生成 MBR、DBR、extended DBR 和 `boot.bin`，现有 QEMU/Bochs helper 语义保持不变。

备选方案是把现有 `boot.bin` 泛化为多 backend 共享 loader，但这会在 spike 阶段扩大对 real-mode/protected-mode/long-mode、exFAT loader 和 UEFI app ABI 的同时修改面。并行 backend 更符合“低风险试探”：先验证 handoff 与 runtime 边界，再决定是否抽取共享 ELF loader 规则。

### Decision: 使用 `BootInfo v2` 作为 UEFI primary handoff

UEFI loader 进入 kernel 前构造 `BootInfoHeader + tagged sections`，core section 的 boot protocol 设为 UEFI，memory map section 使用 `BootMemoryRegion`。kernel 不直接消费 UEFI raw descriptor，也不调用 UEFI Boot Services。UEFI 路径不复用 `BootInfoCore.exfat_data_area_lba` 表达 ESP 或 UEFI storage 语义；该字段在 UEFI core section 中写入零值并视为无 Legacy exFAT data-area metadata。需要传递 ESP/root device/loader storage 信息时，新增可选 storage metadata section。

备选方案是新增 UEFI 专用 handoff 结构，但会绕开已经完成的 unified handoff ABI，并增加 kernel 入口分支。复用 `BootInfo v2` 更能检验前序架构边界。

### Decision: 增加可选 loader metadata section

`BootInfo v2` 增加一个可选 loader metadata section，用于记录 UEFI loader 可审计信息，例如 boot backend、loader build id/version、UEFI firmware vendor/revision、ESP 路径或镜像来源摘要。kernel 初始化不依赖该 section 才能启动；它主要服务于验证、调试和后续 parity 审计。

备选方案是暂不记录 loader metadata，或者把这些调试信息塞进 core section。前者会降低 UEFI spike 的可诊断性，后者会污染 core ABI 并增加必需字段兼容压力。可选 section 更符合 tagged sections 的演进方向。

### Decision: UEFI loader 自带最小 ELF64 reader

UEFI loader 读取 ESP 上的 kernel ELF，校验 ELF64、x86_64 machine、program header 边界和 load segment 范围，然后通过 UEFI page allocation 或等价策略放置内核段。它遵循与 BIOS loader 相同的 ELF64 加载规则，但不复用 BIOS loader 的 ATA/exFAT 读取代码。

备选方案是让 UEFI loader 直接加载 flat binary 或复用 kernel 内的 ELF loader，但会改变现有 kernel artifact 和启动契约。保持 ELF64 kernel artifact 不变能最大程度降低 runtime 影响。

### Decision: 使用 `clang + lld-link` 生成 PE/COFF UEFI app

本地环境已具备 Homebrew LLVM `clang` 和 Homebrew LLD `lld-link`，并且 `gnu-efi` 在 Homebrew 中不可用。UEFI app 构建采用最小 UEFI headers、freestanding flags、Windows x86_64/PE 风格入口和 `lld-link` 输出 `BOOTX64.EFI`。

备选方案包括 vendor edk2、依赖 `gnu-efi` 或使用 ELF 到 PE/COFF 转换。edk2 太重，`gnu-efi` 本地不可用，objcopy 转换链路可作为 fallback 但更容易隐藏 PE/COFF ABI 问题。

### Decision: ESP/FAT 镜像使用 `mtools` 生成

UEFI 路径新增独立 ESP/FAT 镜像，并用 `mformat`、`mmd`、`mcopy`、`mdir` 在用户态填充文件，避免依赖 host mount、loop device 或 macOS disk attach。镜像内容与 Legacy BIOS raw image 隔离。

备选方案是复用现有 raw exFAT image，但 UEFI firmware 的标准 removable media path 需要 FAT ESP，且复用 exFAT raw image 会混淆 BIOS 与 UEFI 产物边界。

### Decision: QEMU + OVMF 是首选 UEFI 验证路径

UEFI smoke 使用 QEMU x86_64 + edk2/OVMF code 固件，并复制 vars template 到 build 输出目录作为可写 NVRAM。Apple Silicon 主机使用 TCG 模拟 x86_64，优先 headless 串口验证。初期验收不止“进入 kernel”：UEFI 路径必须打包默认 PID-1 init 和 `/bin/sh`，并达到与 Legacy BIOS 默认 headless 路径相同的默认 init/user exec marker，例如当前 baseline 的 `BIGOS_USER_EXEC`。

备选方案是 Bochs UEFI 或实体机验证。Bochs 继续作为 Legacy BIOS 低层调试路径，UEFI spike 不依赖 Bochs UEFI；实体机验证不适合作为初期可重复 smoke。

### Decision: 本 change 更新双语架构文档

UEFI spike 不只停留在 OpenSpec validation notes；实现完成时需要更新 `docs/en` 与 `docs/zh` 的对应架构文档，说明 UEFI backend 状态、工具链/OVMF/mtools 假设、BootInfo storage/loader metadata sections、Legacy 与 UEFI 调试入口边界，以及 runtime parity 状态。

备选方案是只记录 validation notes，但该路径会让后续开发者难以从常规架构文档理解 UEFI backend 的边界。双语文档更新更符合仓库文档同步规则。

## Risks / Trade-offs

- [Risk] Apple Silicon 上 x86_64 QEMU TCG 慢，可能导致 smoke 超时。→ Mitigation: UEFI smoke 使用较小 payload、headless 串口 marker 和可配置 timeout，并记录运行环境。
- [Risk] UEFI memory map 到 `BootMemoryRegion` 的类型映射不完整。→ Mitigation: 保守映射未知类型为 reserved，保留 source type/source value/attributes，不释放 runtime、MMIO、ACPI 和未知区域。
- [Risk] `ExitBootServices` 对 memory map key 敏感，loader 分配内存后 key 可能失效。→ Mitigation: 在最终分配完成后重新获取 memory map，并在失败时重试一次或明确报错。
- [Risk] UEFI loader 与 BIOS loader ELF 加载规则分叉。→ Mitigation: 在 spec 中规定共同 ELF64 加载约束，后续可抽取共享文档或测试用例。
- [Risk] 新工具链路径在不同开发机上不一致。→ Mitigation: 构建脚本先做 preflight，可通过环境变量覆盖 `clang`、`lld-link`、OVMF 和 mtools 路径。
- [Risk] UEFI smoke 进入 kernel 但未达到默认 PID-1 init 与 `/bin/sh` marker。→ Mitigation: 将默认 init/user exec marker 作为本 change 的 UEFI smoke 验收目标，未达到时记录为 blocked/failed 而不是 passed。

## Migration Plan

1. 新增 UEFI loader 源码和最小 UEFI ABI/header/glue，构建出 `BOOTX64.EFI`。
2. 新增 xmake `boot-uefi` 或等价目标，preflight 检查 `clang`、`lld-link` 和 LLVM 工具。
3. 扩展 Python helper 生成独立 ESP/FAT 镜像，打包 UEFI loader、kernel 和用户态 payload。
4. 新增 QEMU/OVMF 启动入口，复制 vars template 到 build 输出目录，输出独立串口日志。
5. 验证 UEFI loader 能进入 kernel、启动默认 PID-1 init 和 `/bin/sh`，并观察到与 Legacy BIOS 默认 headless 路径相同的默认 init/user exec marker。
6. 更新双语架构文档，记录 UEFI spike 状态、metadata sections、工具链假设和 Legacy/UEFI 调试入口边界。
7. 保留现有 BIOS/QEMU/Bochs validation 作为回归保护；若 UEFI 路径失败，可删除/忽略新增 UEFI 产物，不影响 Legacy backend。

## Open Questions

- 无。已决定：UEFI core section 中 `exfat_data_area_lba` 写零值并不承载 ESP 语义；storage metadata 使用独立可选 section；UEFI smoke 目标达到默认 PID-1 init 与 `/bin/sh` 的 Legacy 等价 marker；新增可选 loader metadata section；本 change 更新双语架构文档。
