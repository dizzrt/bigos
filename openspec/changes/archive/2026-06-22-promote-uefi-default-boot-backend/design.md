## Context

BigOS 当前已经具备两条 x86_64 boot backend：Legacy BIOS/MBR/exFAT 是历史默认可运行路径，UEFI backend 已经作为可运行 spike 存在。UEFI spike 验证了 `BOOTX64.EFI`、ESP/FAT 镜像、QEMU + OVMF、`BootInfo v2` handoff、UEFI memory map 规范化和默认用户态 payload 打包，但规格仍把它定位为 parallel/non-default backend。

本 change 的目标不是再新增一个 loader spike，而是提升现有 UEFI backend 的默认身份和 runtime parity 责任：默认 boot backend 应经由 UEFI 到达现有 bounded userland baseline。Legacy BIOS 在本 change 中不删除、不重写，仍作为显式选择的兼容 backend；后续对 Legacy BIOS 降级交叉验证和全量默认 smoke 矩阵对齐可由后续独立工作继续收敛。

默认 UEFI 数据流保持为：

```text
default boot selection
    |
    v
UEFI ESP/FAT image
    |
    |-- EFI/BOOT/BOOTX64.EFI
    |-- kernel ELF
    |-- /boot/user/init.elf
    |-- /bin/sh and bounded /bin/* payload
    v
UEFI loader
    |
    |-- validate/load existing ELF64 kernel
    |-- normalize UEFI memory map to BootMemoryRegion[]
    |-- build BootInfo v2 core + memory map + optional metadata
    |-- ExitBootServices
    v
x86_64 kernel entry(BootInfoHeader*)
    |
    v
existing bounded userland baseline
```

## Goals / Non-Goals

**Goals:**

- 将 UEFI backend 提升为默认可运行 boot backend。
- 默认 UEFI boot 必须打包并启动现有 bounded userland baseline，包括 resident PID-1 init、`/bin/sh` 和默认 `/bin/*` payload。
- 保持 `BootInfo v2` 为 UEFI primary handoff ABI，kernel 不直接消费 UEFI Boot Services 或 raw firmware descriptors。
- 保持 kernel link address、entry ABI、页表地址假设、IDT/syscall vectors、用户态 ABI 和当前 runtime 子系统初始化顺序不被静默改变。
- 明确 Legacy BIOS 仍作为显式选择的可运行 backend 保留；本 change 不删除其源码、产物或现有运行入口。
- 使用 QEMU + OVMF headless 串口输出作为默认 UEFI boot 验收的首选证据，缺少依赖时明确记录 blocked/skipped 和残余风险。

**Non-Goals:**

- 不实现 Secure Boot、GOP framebuffer/Unicode console、ACPI table handoff、UEFI Runtime Services 或 NVRAM 持久语义。
- 不引入第二 ISA，不扩大到 AArch64/RISC-V backend。
- 不引入 SMP 扩展、新存储驱动、virtio/AHCI/NVMe 运行时支持或完整设备模型。
- 不实现完整 POSIX、动态链接、完整 libc、广泛 file-backed `mmap` 或完整持久文件系统。
- 不把后续 Legacy BIOS 交叉验证整理、全量默认 run target 文档重排和完整 smoke matrix 产品化全部并入本 change。

## Decisions

### Decision: UEFI 成为默认 backend，Legacy BIOS 保留为显式 backend

默认 boot/run 选择指向 UEFI backend；Legacy BIOS 入口继续存在并需要显式选择。这样能让项目主线开始以现代 firmware backend 为默认目标，同时保留早期 BIOS/ATA/Bochs 调试路径作为回退和交叉验证基础。

备选方案是继续保持 Legacy BIOS 默认，只新增 UEFI parity smoke。该方案不能完成默认 backend 晋升目标，会让后续 framebuffer、ACPI 和现代 storage/networking 工作继续依赖非默认路径。

### Decision: runtime parity 以现有 bounded userland baseline 为界

UEFI parity 不定义为完整 POSIX 或完整硬件平台等价，而是定义为到达当前默认用户可见基线：kernel 初始化、进程/exec、resident init、shell 启动、默认用户态 payload 和已有串口可观察行为。这样避免把默认 backend 晋升扩大成 libc、filesystem、job control 或设备生态重写。

备选方案是要求 UEFI 与 Legacy BIOS 在所有调试入口、显示路径、存储语义和 smoke matrix 上完全一致。该方案会把后续交叉验证和 smoke 矩阵产品化工作压入本 change，增大 blast radius。

### Decision: BootInfo v2 ABI 不因默认 backend 改变

UEFI 继续通过 `BootInfoHeader*` 进入 kernel，core section 标识 UEFI，memory map section 使用 `BootMemoryRegion`，可选 storage/loader metadata section 仍保持可选。UEFI core 不复用 Legacy exFAT 字段表达 ESP 语义；kernel 仍只消费规范化 handoff。

备选方案是为默认 UEFI 路径新增专用 kernel entry 或专用 handoff struct。该方案会绕开已有 unified handoff 边界，并使 Legacy/UEFI runtime parity 更难审计。

### Decision: 默认 UEFI 验收基于串口可观察用户态行为

本 change 的默认 UEFI 验收需要观察现有 init/shell/user exec 行为的串口证据。成功进入 kernel 但未到达默认用户态基线不能算作通过；缺少 QEMU、OVMF、mtools、LLVM/LLD、xmake 或 cross toolchain 时只能记录 blocked/skipped。

备选方案是只验证 `BOOTX64.EFI` 形态或 kernel 入口。这对 loader spike 足够，但不足以证明默认 boot backend 的用户可见等价性。

### Decision: 文档描述从 spike 晋升为有界默认 parity

相关双语架构文档需要从“UEFI spike/non-default”更新为“UEFI default runnable backend within bounded userland baseline”。同时必须保留清晰非目标，避免把 UEFI 默认化误读为 Secure Boot、framebuffer console、ACPI、Runtime Services 或第二 ISA 已完成。

备选方案是只更新 OpenSpec，不更新常规文档。该方案会让后续开发者从 docs 中继续看到过期的 spike 状态。

## Risks / Trade-offs

- [Risk] UEFI 默认化可能掩盖 Legacy BIOS 回归。→ Mitigation: 本 change 保留 Legacy BIOS 显式入口并加入不覆盖、不删除、不改变 boot protocol 的 review/validation 任务；完整交叉验证整理留给后续任务。
- [Risk] QEMU + OVMF 在不同主机上的固件路径和 vars template 不一致。→ Mitigation: preflight 必须报告缺失路径，允许本地配置覆盖，并将缺失依赖记录为 blocked/skipped。
- [Risk] UEFI memory map 类型映射错误会污染 early free page pool。→ Mitigation: 继续保守处理 unknown/runtime/MMIO/ACPI/firmware-reserved 类型，并加入初始化顺序和 free-pool review。
- [Risk] 默认 backend 切换可能影响开发者习惯命令。→ Mitigation: 文档明确默认入口和显式 Legacy 入口；错误信息必须能说明当前选择的 backend。
- [Risk] Apple Silicon 上 x86_64 QEMU + OVMF TCG 较慢导致 timeout。→ Mitigation: UEFI smoke 使用 bounded timeout，并记录 timeout、串口日志和残余风险。

## Migration Plan

1. 复查现有 UEFI spike 的 loader、ESP 打包、OVMF 启动和 BootInfo handoff，确认已具备默认用户态 payload 打包基础。
2. 调整默认 backend 选择，使默认 boot/run 路径使用 UEFI，同时保留显式 Legacy BIOS 路径。
3. 补齐 UEFI 默认用户态基线验收，确保 QEMU + OVMF headless 路径能观察到现有 init/shell/user exec 行为。
4. 复查 BootInfo、memory map、kernel link/entry、page-table 和 ABI 假设，确认没有因默认化产生静默变更。
5. 更新双语架构文档和路线图状态，描述 UEFI 有界默认 parity 和明确非目标。
6. 运行最窄可用构建、静态检查、UEFI headless smoke 和 Legacy BIOS 不回归检查；不可用项记录具体 blocker 与残余风险。

## Open Questions

- 无。默认选择 UEFI；Legacy BIOS 保留为显式 backend；runtime parity 边界限定为当前 bounded userland baseline；GOP framebuffer、ACPI、Runtime Services 和完整交叉验证矩阵留给后续独立工作。
