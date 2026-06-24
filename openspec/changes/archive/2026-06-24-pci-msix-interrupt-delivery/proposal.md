## Why

BigOS 即将接入现代 PCI 设备（首版目标 virtio-blk），这类设备使用 MSI-X 作为高效的中断投递方式。当前内核虽有 LAPIC/IOAPIC 基础，但没有 PCI MSI-X capability 解析、MSI-X table/PBA 映射与 message 编程能力。需要在 `pci-config-access-and-vector-alloc` 提供的 PCI 配置访问与向量分配之上，新增一个有界的 MSI-X 中断投递能力，作为现代块存储驱动消费完成中断的前置。

## What Changes

- 新增有界 MSI-X 中断投递能力：解析设备 MSI-X capability、定位并映射 MSI-X table 与 PBA 所在 BAR、编程 message address/data 指向已分配的内核向量、设置/清除 per-vector mask、启用/禁用 MSI-X function-level enable。
- 复用 `kernel-irq-vector-allocation` 为每个 MSI-X 向量分配内核中断向量并注册 handler，复用现有 LAPIC EOI 所有权，不引入独立 EOI 路径。
- 复用 `pci-config-access` 读取 capability 与 BAR 描述；MSI-X table/PBA 的 MMIO 映射通过现有内核虚拟内存能力按需建立有界映射。
- 明确首版边界：仅 INTx 之外的 MSI-X 路径，单一或少量向量，function-level mask 与 per-vector mask 均支持；不实现 MSI（非 X）、IRQ affinity 动态迁移、SMP 目标重排或中断合并。
- 增加默认关闭验证：用可控测试设备或可控 producer 触发 MSI-X 向量，验证 message 编程、mask/unmask、向量投递与 handler 执行闭环，以及环境不可用时的跳过记录。
- 不改变当前 Legacy BIOS 启动 ABI、链接地址、页表布局、既有中断/系统调用向量分配、磁盘镜像布局或默认 userland baseline。

## Capabilities

### New Capabilities

- `pci-msix-interrupt-delivery`: 有界 PCI MSI-X capability 解析、table/PBA 映射、message 编程、mask 控制与向量投递契约。

### Modified Capabilities

- 无。`pci-config-access`、`kernel-irq-vector-allocation`、`apic-default-interrupt-delivery` 等能力保持原有契约；本变更在其之上新增 MSI-X 投递能力。

## Impact

- 影响子系统：`kernel/drivers` 下新增 MSI-X 模块、`kernel/core/irq` 与向量分配的对接、`kernel/mm` 的 MMIO 映射使用、默认关闭验证入口与构建开关。
- 依赖：依赖 `pci-config-access-and-vector-alloc` 已完成（PCI 配置访问 + capability/BAR 读取 + 向量分配）。
- 架构假设：x86_64 freestanding 内核，复用现有 LAPIC EOI；MSI-X message address 指向 LAPIC 区域，data 指向已分配向量。
- 内存布局假设：不改变内核 higher-half、boot handoff、页表自映射、direct map 或 CR3 切换；MSI-X table/PBA 通过现有内核虚拟内存映射按需建立有界 MMIO 映射。
- 中断假设：MSI-X 向量复用现有静态 IDT、`InterruptFrame` ABI 和 LAPIC EOI 所有权；不发送 PIC EOI，不改变异常/syscall 语义。
- 仿真器假设：优先使用 QEMU 验证 MSI-X 投递（必要时用可配置测试设备或可控 producer）；Bochs 若不支持则记录为跳过。
- 工具链假设：xmake + x86_64-elf-gcc/g++；Python helper/验证脚本如需修改命令通过 `uv run ...` 记录。
- 非目标：MSI（非 X）、IRQ affinity 动态迁移、SMP 中断目标重排、中断合并/限流、IOMMU/中断重映射、用户可见中断 ABI、完整设备热插拔。
