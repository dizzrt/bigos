## Why

BigOS 已具备设备框架、PCI/MSI-X 投递、异步 I/O 完成和现代块设备驱动的基础，下一步需要把这些能力扩展到网络设备层，为后续有界协议路径与最小 socket ABI 提供真实设备入口。

本变更影响内核设备/驱动、PCI/MSI-X、中断完成、缓冲区管理和默认关闭运行时验证路径；不改变启动 ABI、磁盘布局、系统调用 ABI 或用户态可见网络接口。

## What Changes

- 新增 modern-only virtio-net 网络设备驱动，基于既有 PCI capability、MMIO、MSI-X 和设备发布框架完成探测、feature 协商、virtqueue 初始化和设备状态管理。
- 提供有界的 RX/TX split virtqueue 路径，支持接收缓冲预投递、发送 descriptor 链提交、used ring 完成回收和确定性错误处理。
- 将 virtio-net 完成中断接入既有中断分发与 LAPIC EOI 边界，IRQ 入口保持 allocation-free、nonblocking，不做协议栈或用户态唤醒语义。
- 提供基于通用 `bigos::device` 的内核内部网络设备发布与诊断状态，供后续协议层显式选择；不创建用户可见设备节点、socket syscall、协议栈或网络配置接口。
- 提取小型 virtio common helper，复用 virtio PCI capability、common cfg、status/feature、split queue 配置和 notify 等 transport 机制，同时保持 virtio-blk 设备语义不被重构。
- 增加默认关闭的 QEMU tap 验证路径和宿主侧最小 tap 配置/清理脚本，覆盖设备发布、RX/TX 队列初始化、TX 完成、可控 RX 包接收、错误/timeout 诊断和默认启动回归。
- 非目标：不实现完整网络栈、IP/ARP/ICMP/TCP/UDP、socket/fd/syscall ABI、DHCP、DNS、路由、防火墙、多队列、packed virtqueue、legacy/transitional virtio、热插拔、网卡用户态配置或跨 ISA 后端。

## Capabilities

### New Capabilities

- `virtio-net-driver`: 定义 BigOS 内核内部 modern-only virtio-net 网络设备驱动、RX/TX 有界队列、中断完成、设备发布与验证边界。

### Modified Capabilities

- None.

## Impact

- 受影响子系统：`kernel/drivers` 下的设备驱动、PCI/virtio 支撑代码、IRQ/MSI-X 完成路径、`bigos::device` 内核内部设备发布/选择边界、构建开关、默认关闭 smoke 验证和 tap 配置辅助工具。
- 架构假设：x86_64 当前主线；不接入新 ISA，不改变 boot handoff、链接地址、页表布局、中断/系统调用向量或 CR3 切换约束。
- 内存与 DMA 假设：使用显式分配的有界、对齐内核内存作为 virtqueue 和包缓冲；不依赖 direct map 覆盖设备 MMIO，不引入 IOMMU 或无限动态缓冲池。
- 仿真器假设：验证优先使用支持 modern virtio-net、MSI-X 与 tap 后端的 QEMU，并通过本变更提供的宿主侧最小 tap 配置/清理脚本准备 RX 注入环境；Bochs 可用于默认启动/中断边界回归，但不要求其提供 virtio-net 设备支持。
- 磁盘布局假设：不改变 boot disk、exFAT、`/rw` 或 userland baseline；网络设备验证不得替换启动存储设备。
- 工具链假设：继续使用 xmake 与 x86_64-elf 交叉工具链；Python 辅助验证命令通过 `uv run ...` 执行。
