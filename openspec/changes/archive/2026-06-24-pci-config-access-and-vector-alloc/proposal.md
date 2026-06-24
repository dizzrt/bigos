## Why

BigOS 已有 LAPIC、IOAPIC、IPI 等 APIC 基础，但完全没有 PCI 配置空间访问能力，外部中断向量也以写死常量（如 timer、keyboard、LAPIC timer、IPI）的方式静态分配。要继续接入现代 PCI 设备（首版目标是 virtio-blk）和 MSI-X 中断，需要先有一个有界的 PCI 配置访问层和一个可复用的内核中断向量分配/注册边界，作为后续 MSI-X 与现代块存储驱动的共享前置能力。

## What Changes

- 新增有界 PCI 配置空间访问能力：首版采用传统 IO 端口机制（CONFIG_ADDRESS 0xCF8 / CONFIG_DATA 0xCFC），支持按 bus/device/function 读取配置头、遍历 capability list、读取 BAR 描述，不引入 ECAM/MMCONFIG、热插拔、完整总线枚举树或 PCIe 扩展能力。
- 新增有界内核中断向量分配能力：在现有静态 IDT、`VectorOwner` 分类和 LAPIC EOI 边界之上，提供从一个受限动态向量区间分配/释放/注册可屏蔽外部中断向量的接口，复用现有 ISR 注册与 LAPIC EOI 所有权，不改变现有写死向量、syscall 向量或异常向量语义。
- 明确两个能力的上下文边界：配置访问与向量分配只在普通可阻塞内核初始化/驱动上下文进行，不在 IRQ 上下文、调度临界区或抢占禁用区做分配；向量注册产物与现有中断分发 ABI 兼容。
- 增加默认关闭的源级或运行时验证入口，覆盖 PCI 设备存在性探测、capability 遍历、BAR 读取、向量分配/释放/重复释放边界，以及环境不可用时的跳过记录。
- 不改变当前 Legacy BIOS 启动 ABI、链接地址、页表布局、既有中断/系统调用向量分配、磁盘镜像布局或默认 ATA/exFAT/userland baseline。

## Capabilities

### New Capabilities

- `pci-config-access`: 有界 PCI 配置空间访问、capability list 遍历与 BAR 描述读取契约。
- `kernel-irq-vector-allocation`: 在现有 APIC/LAPIC EOI 边界之上的有界内核中断向量分配/释放/注册契约。

### Modified Capabilities

- 无。既有 `apic-default-interrupt-delivery`、`smp-ipi-tlb-shootdown`、`interrupt-exception-foundation`、`device-driver-framework` 等能力保持原有契约；本变更只在其之上新增共享前置能力。

## Impact

- 影响子系统：`kernel/drivers` 下新增 PCI 配置访问驱动、`kernel/core/irq` 向量分配/注册路径、可能扩展 `include/irq` 与 `include/drivers` 公共头、默认关闭验证入口与构建开关。
- 架构假设：目标仍为 x86_64 freestanding 内核；复用现有 LAPIC/IOAPIC，不要求 ECAM、IOMMU 或跨 ISA。
- 内存布局假设：不改变内核 higher-half、boot handoff、页表自映射、direct map 或 CR3 切换语义；BAR/MMIO 若需访问由后续 MSI-X 变更按需建立映射，本变更只读取 BAR 描述而不强制映射设备内存。
- 中断假设：复用现有静态 IDT、`InterruptFrame` ABI、`VectorOwner` 分类和 LAPIC EOI 所有权；新增动态向量只占用受限保留区间，不与异常、syscall、PIC fallback、LAPIC timer、IPI 向量冲突。
- 仿真器假设：优先使用 QEMU 验证 PCI 设备探测；Bochs 若不支持相应配置则记录为跳过。
- 工具链假设：使用 xmake 与 x86_64-elf-gcc/x86_64-elf-g++；Python helper 或验证脚本如需修改，命令通过 `uv run ...` 记录。
- 非目标：ECAM/MMCONFIG、PCIe 扩展 capability、完整 PCI 总线枚举树、热插拔、电源管理、MSI/MSI-X 编程本身（属于后续变更）、IRQ affinity 重排、用户可见设备节点或新 syscall ABI。
