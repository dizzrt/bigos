## Why

当前驱动初始化仍由各消费方直接调用具体硬件后端，块设备、IRQ 芯片、计时器和显示等路径缺少统一的注册、探测和查找边界。这会让后续块层、第二块设备后端和更多设备类型继续把硬件假设扩散到 VFS、文件系统和内核策略中；本 change 先建立一个 freestanding-safe 的设备与驱动框架，为后续扩展提供受控入口。

## What Changes

- 新增一个内核内设备与驱动注册框架，支持有界静态容量、设备类别、驱动匹配、probe 状态和确定性错误返回。
- 新增设备发现/发布边界：现有 ATA PIO 块设备、PIT timer、VGA text device、CMOS RTC 先通过框架注册并 probe，再由相关内核路径按设备类别或内部稳定角色获取。
- 保留现有同步块设备 `BlockDevice` 读写契约，但把默认 boot disk 和 persistent test disk 的选择从消费方直接初始化收束到设备框架。
- 为当前接入的设备类型提供最小通用字段：设备类别、实例 id、内部稳定角色、driver 私有上下文、flags/capabilities 和生命周期状态；不引入动态热插拔或完整 bus model。
- 补充默认关闭的源码级和运行时验证要求，覆盖重复注册、probe 失败、未探测设备不可发布、块设备查找、现有 boot asset 读取和 persistent `/rw` 路径不回退。
- 保持当前 x86_64 Legacy BIOS 默认运行目标、单核同步模型、现有磁盘布局、现有中断向量和 bounded userland 行为。

## Capabilities

### New Capabilities

- `device-driver-framework`: 定义 freestanding-safe 的设备、驱动注册/探测/发布框架，以及内核子系统如何获取已探测设备。

### Modified Capabilities

- `block-device-read`: 明确现有同步块设备后端必须可通过设备框架注册、probe 和查找，同时保持既有读写边界、错误语义和 Legacy BIOS boot disk 行为。

## Impact

- 影响子系统：`kernel/drivers`、`include/drivers`、`kernel/core/fs` 中的 VFS/bigfs 块设备接入路径，VGA/PIT/CMOS RTC 的正常初始化路径，以及需要初始化设备框架的内核启动顺序。
- API/ABI：新增内核内部设备/驱动注册 API；不新增用户态 syscall，不改变用户态 ABI、启动 ABI、IDT/syscall vector、页表布局、链接地址、MBR/exFAT boot asset 布局或现有 `BlockDevice` 读写函数语义。
- 架构假设：当前交付目标仍为单核 x86_64 Legacy BIOS 路径；UEFI runtime parity、多架构后端和 SMP 不在本 change 范围。
- 内存/初始化假设：框架应使用静态或有界内核存储，不能依赖 hosted runtime、异常、RTTI、动态链接或不可控初始化顺序；probe 可在普通内核初始化/可阻塞上下文运行，不能从 IRQ path 发起阻塞 IO。
- 存储假设：复用现有 ATA PIO boot disk 与 persistent test disk 后端，不引入 request queue、async I/O、virtio、AHCI/SATA、NVMe 或新的块设备后端。
- 工具链/验证假设：实现验证以 xmake、`x86_64-elf-*` 交叉工具链、QEMU/Bochs 可用性和默认关闭 smoke 为前提；环境不可用时记录 skipped/blocked 和残余风险，不声称 runtime passed。
