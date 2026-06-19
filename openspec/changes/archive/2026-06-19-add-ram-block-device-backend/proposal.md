## Why

设备注册/probe 框架和块 I/O 请求层已经具备基础契约，但当前默认存储路径仍主要依赖 ATA PIO 这一类硬件块后端，缺少一个独立的第二块设备后端来证明框架可以承载不同实现并保持相同块设备、请求层和 cache 语义。
本变更引入有界 RAM 块设备后端，作为内核内部验证后端，帮助验证设备框架、块请求层和缓存路径的可扩展性，而不引入新 ISA 或新的硬件存储栈。

## What Changes

- 新增 freestanding-safe 的 RAM 块设备后端，提供固定容量、整扇区读写、确定性状态和可重复初始化语义。
- 将 RAM 块后端接入设备与驱动注册/probe 框架，使其可作为第二个块设备后端发布，并通过稳定内部角色被验证路径查找。
- 让该后端通过现有块 I/O 请求层和 page/buffer cache 路径参与读写验证，证明上层消费者不依赖具体 ATA PIO 初始化。
- 保持现有 ATA PIO、boot disk、persistent `/rw`、MBR/exFAT/bigfs、用户态 ABI 和 x86_64 Legacy BIOS 默认启动路径不变。
- 明确非目标：不新增 virtio、AHCI/SATA、NVMe、USB storage、PCI/ACPI 枚举、UEFI runtime parity、新 ISA、async I/O、SMP I/O 或用户可见设备节点。

## Capabilities

### New Capabilities

- `ram-block-device-backend`: 定义内核 RAM 块设备后端的固定容量、整扇区读写、probe/publication、请求层/cache 对接和确定性验证契约。

### Modified Capabilities

- `device-driver-framework`: 扩展设备框架契约，使同一块设备类别可以发布并查找第二个非 ATA 后端，同时保持稳定内部角色、probe 状态和用户不可见边界。
- `block-io-request-layer`: 扩展请求层验证契约，要求请求层可以面向第二个已发布块后端执行同步读写，并保持按设备队列隔离与确定性状态传播。

## Impact

- 影响子系统：内核设备框架、块设备层、块 I/O 请求层、page/buffer cache、RAM-backed 验证路径，以及相关默认关闭 smoke。
- 架构假设：继续以 x86_64 Legacy BIOS/MBR/exFAT/bigfs 为默认运行目标；UEFI 后端仍是非 runtime-parity spike；不接入新 ISA。
- 内存与布局假设：RAM 块后端使用有界静态或显式分配容量，不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局或用户态 ABI。
- 上下文假设：probe、读写请求和 cache 验证只在允许阻塞的普通内核上下文运行；不把后端声明为 IRQ-safe、preemption-disabled-safe 或异步。
- 工具链与模拟器假设：继续使用 xmake、x86_64-elf 工具链，以及 QEMU/Bochs 在可用范围内执行默认关闭 smoke；环境不可用时需要记录跳过原因和残余风险。
