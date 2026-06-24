## Why

BigOS 已具备设备框架、块请求层、IRQ-safe completion 与非轮询块路径，但真实存储后端仍停留在 ATA PIO legacy 路径。为验证这些内核内部抽象能承载现代硬件风格后端，需要引入首版现代块存储驱动 virtio-blk，并采用 modern-only virtio PCI transport，通过 MSI-X 投递完成中断接入块请求层 completion。本变更依赖已拆分的 PCI 访问/向量分配能力与 MSI-X 投递能力。

## What Changes

- 新增现代块存储驱动能力：modern-only virtio-blk，通过 PCI capability 解析 virtio common/notify/ISR/device cfg，初始化 split virtqueue，提交有界块读写请求。
- 通过既有设备/驱动 registry 注册、probe、发布该块设备后端，使用内核内部稳定角色，不引入用户可见设备节点或新 syscall ABI。
- 让驱动通过既有块请求层 issue/completion 边界接收请求；virtqueue used buffer 通知通过 MSI-X 向量投递，IRQ-safe completion 入口发布 terminal reason 并唤醒等待者。
- 明确首版硬件发现、队列深度、请求大小、错误恢复与诊断边界，保持 IRQ-safe、freestanding-safe、固定/静态容量约束。
- 增加默认关闭验证，覆盖设备发布、基础读写、错误/timeout、MSI-X 完成闭环或 skipped runtime coverage 记录。
- 不改变当前 Legacy BIOS 启动 ABI、链接地址、页表布局、中断/系统调用向量分配、磁盘镜像布局与默认 ATA/exFAT/userland baseline。

## Capabilities

### New Capabilities

- `modern-block-storage-driver`: modern-only virtio-blk 的设备发现、发布、virtqueue 提交、MSI-X 完成、诊断与验证契约。

### Modified Capabilities

- 无。既有 `device-driver-framework`、`block-io-request-layer`、`interrupt-driven-io-completion`、`nonpolling-block-path`、`pci-config-access`、`kernel-irq-vector-allocation`、`pci-msix-interrupt-delivery` 等能力保持原有契约；本变更在其之上新增具体后端能力。

## Impact

- 影响子系统：`kernel/drivers` 下新增 virtio-blk 驱动、设备框架注册/probe、块请求层 issue/completion 调用点、MSI-X 向量绑定、MMIO/PCI 访问、默认关闭验证入口与构建开关。
- 依赖：依赖 `pci-config-access-and-vector-alloc` 与 `pci-msix-interrupt-delivery` 已完成。
- 架构假设：x86_64 freestanding 内核，单核调度；virtio modern PCI transport + split virtqueue + MSI-X 完成中断；不要求 SMP、IOMMU、跨 ISA。
- 内存布局假设：不改变内核 higher-half、boot handoff、页表自映射、direct map 或 CR3 切换；virtqueue 环与 virtio cfg/MMIO 通过现有内核虚拟内存能力建立有界映射，DMA 缓冲使用内核物理页并以物理地址提供给设备。
- 中断假设：完成中断走 MSI-X 向量，复用现有 LAPIC EOI 所有权；IRQ-safe completion 入口只发布状态并唤醒等待者，不发送 PIC EOI、不阻塞、不分配、不做 cache/filesystem policy。
- 仿真器假设：优先使用 QEMU modern-only virtio-blk（MSI-X）验证；Bochs 若不支持则记录为跳过。
- 磁盘布局假设：首版不改变 boot disk、exFAT 启动介质与 persistent `/rw` 布局契约；是否作为默认文件系统后端留给后续集成变更。
- 工具链假设：xmake + x86_64-elf-gcc/g++；Python helper/验证脚本如需修改命令通过 `uv run ...` 记录。
- 非目标：legacy/transitional virtio、INTx 完成路径、indirect/packed virtqueue、多队列调度、virtio-blk 全特性、NVMe、DMA scatter-gather 复杂化、用户态 async I/O、默认替换 ATA 启动路径、完整 PCI 枚举/热插拔。
