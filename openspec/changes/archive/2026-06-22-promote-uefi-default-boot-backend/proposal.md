## Why

现有 x86_64 UEFI backend 已经从蓝图推进为可运行 spike，但项目默认可运行基线仍以 Legacy BIOS/MBR/exFAT 为中心，UEFI 还没有承担默认 boot backend 的责任。本 change 需要把 UEFI 从 non-parity spike 提升为默认可运行 backend，并证明它能到达当前 bounded userland baseline。

## What Changes

- 将 x86_64 UEFI backend 的规格从 spike 状态提升为默认可运行 boot backend：默认启动语义以 UEFI 路径为主，但不删除 Legacy BIOS backend。
- 要求 UEFI 默认启动路径打包现有 kernel、resident PID-1 init、`/bin/sh` 和默认 `/bin/*` 用户态 payload，并到达当前 bounded userland baseline。
- 收紧 UEFI handoff/runtime parity 要求：UEFI loader 继续使用 `BootInfo v2`、规范化 memory map、可选 storage/loader metadata，并保持 kernel link address、entry ABI、分页地址与早期内存初始化假设不被静默改变。
- 引入默认 backend 选择与回退边界：UEFI 成为默认可运行 backend；Legacy BIOS 继续作为显式选择的可运行 backend 和后续交叉验证对象。
- 增加 UEFI 默认 boot 验收要求：默认 UEFI headless 路径必须以串口可观察结果证明进入现有 init/shell/user exec 行为；缺少 QEMU、OVMF、mtools、LLVM/LLD 或 cross toolchain 时必须记录 blocked/skipped 与残余风险。
- 更新相关双语架构文档和路线图状态说明，明确 UEFI 已从 spike 晋升为默认 runtime-parity backend 的有界范围，但不宣称 Secure Boot、GOP framebuffer、ACPI handoff、UEFI Runtime Services、第二 ISA 或完整 POSIX 能力。

## Capabilities

### New Capabilities

- `uefi-default-boot-backend`: 约束 UEFI 成为默认可运行 boot backend、到达 bounded userland baseline、默认/显式 backend 选择、UEFI 默认验收和 Legacy BIOS 非删除边界。

### Modified Capabilities

- `x86-uefi-boot-backend`: 将 UEFI backend 从 spike/非默认状态提升为 runtime parity 的默认可运行路径，并补充默认用户态基线、默认 backend 选择和失败记录要求。
- `uefi-boot-blueprint`: 更新蓝图状态，说明 UEFI 正式化已越过 spike 阶段，但 framebuffer、ACPI、Runtime Services、Secure Boot 和第二 ISA 仍是后续独立能力。
- `runtime-smoke-validation`: 增加默认 UEFI boot smoke 的验收语义，要求默认可见用户态行为通过 UEFI 路径可观察。

## Impact

- 受影响子系统：x86_64 UEFI loader、boot image packaging、`BootInfo v2` handoff、早期内存图消费、默认 boot/run 选择、boot debug helper、runtime smoke validation 和双语架构文档。
- 架构假设：仍仅覆盖 x86_64；不引入 AArch64/RISC-V；kernel higher-half link/entry 地址、IDT/syscall vectors、page-table layout 和用户态 ABI 不因本 change 静默改变。
- 内存布局假设：UEFI memory descriptors 继续在 loader 侧转换为 `BootMemoryRegion`；未知、runtime、MMIO、ACPI 和 firmware-reserved 区域必须保守处理，不得进入初始 free page pool。
- 模拟器假设：UEFI 默认验收以 QEMU + OVMF headless 串口输出为首选；Bochs 仍主要服务 Legacy BIOS 低层交叉验证。
- 磁盘布局假设：UEFI 路径使用独立 ESP/FAT 镜像；Legacy BIOS 路径继续使用现有 MBR/exFAT raw image，不在本 change 中删除或重写。
- 工具链假设：需要 x86_64 cross toolchain、xmake、QEMU、OVMF、mtools、LLVM/LLD；缺失时必须记录具体 blocker、替代检查和剩余 bootability 风险。
- 非目标：不实现 Secure Boot、GOP framebuffer/Unicode console、ACPI table handoff、UEFI Runtime Services、NVRAM 持久语义、SMP 扩展、广泛新存储驱动、完整 POSIX、动态链接或完整 libc。
