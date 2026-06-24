## Context

BigOS 已实现：设备/驱动 registry 与 probe/publish（`device-driver-framework`）、有界块请求层与按设备队列/completion token/generation（`block-io-request-layer`）、IRQ-safe 中断驱动 completion 与 scheduler wakeup（`interrupt-driven-io-completion`）、既有块路径迁出同步轮询（`nonpolling-block-path`）。真实存储后端目前仍是 ATA PIO。

本变更的两个前置能力提供：PCI 配置空间访问、capability 遍历、BAR 描述读取、内核中断向量分配（`pci-config-access-and-vector-alloc`），以及 MSI-X capability 解析、table/PBA 映射、message 编程、mask 控制与向量投递（`pci-msix-interrupt-delivery`）。

本变更在这些能力之上实现首版现代块存储驱动 virtio-blk，采用 modern-only virtio PCI transport：解析 virtio PCI capability（common/notify/ISR/device cfg）、初始化 split virtqueue、提交块读写请求、用 MSI-X 投递 used buffer 完成中断并接入块请求层 completion。默认运行目标仍是 x86_64 Legacy BIOS，QEMU 是主要验证环境。

## Goals / Non-Goals

**Goals:**

- 通过设备框架注册/probe/publish 一个 virtio-blk 块设备后端，使用内核内部稳定角色。
- 实现 modern-only virtio PCI transport：解析 virtio vendor-specific capability，定位 common/notify/ISR/device cfg；完成 device reset、feature 协商（含 VIRTIO_F_VERSION_1）、status 推进。
- 初始化单个 split virtqueue（descriptor table、available ring、used ring），用内核物理页作为 DMA 缓冲并以物理地址提供给设备。
- 通过既有块请求层 issue/completion 边界提交块读写：填充 virtio-blk 请求头/数据/状态，notify 设备，pending 等待。
- 用 MSI-X 向量投递 used buffer 完成中断，IRQ-safe completion 入口解析 used ring、按 token 身份完成请求并唤醒等待者。
- 提供默认关闭验证，覆盖发布、读写往返、错误/timeout、MSI-X 完成闭环与 skipped 记录。

**Non-Goals:**

- 不支持 legacy/transitional virtio 或 INTx 完成路径（首版仅 modern-only + MSI-X）。
- 不实现 indirect descriptor、packed virtqueue、多 virtqueue、event index。
- 不实现 virtio-blk 全特性（如 discard/write-zeroes/flush 全集之外的扩展），首版限定读、写与必要 flush。
- 不实现 NVMe、AHCI、DMA scatter-gather 复杂分散聚合优化。
- 不引入用户态 async I/O、新 syscall 或用户可见设备节点。
- 不默认替换 ATA/exFAT 启动路径，不改变 boot disk/persistent `/rw` 布局；是否成为默认 FS 后端留待后续集成变更。

## Decisions

1. 仅实现 modern-only virtio PCI transport。
   - 原因：modern 是 virtio 1.0+ 标准路径，capability 模型清晰，与现代设备一致；用户已确认走 modern-only。
   - 备选：transitional 兼容 legacy IO 端口。增加旧布局代码且非首版目标。

2. 完成中断采用 MSI-X，而非 INTx。
   - 原因：用户已确认走 MSI-X；MSI-X 由前置 `pci-msix-interrupt-delivery` 提供，per-vector 投递与 EOI 边界清晰，避免 PIC INTx 共享线路由的不确定性。
   - 备选：INTx + PIC。需要额外处理共享中断与 ISR cfg 轮询，且偏离已选方向。

3. 首版单 virtqueue、split ring，队列深度与请求大小为有界静态常量。
   - 原因：单队列足以验证 issue/completion 闭环；split ring 实现简单、可审查；有界容量符合 freestanding 约束。
   - 备选：多队列/packed ring。属于吞吐优化，留作后续。

4. completion 状态仍由块请求层拥有，virtio used ring 解析在 IRQ-safe completion 入口完成后通过 token 完成请求。
   - 原因：复用 `block-io-request-layer` 的 token/generation/timeout/late-completion 机制，保持与 ATA 路径一致的生命周期语义。
   - 备选：virtio 驱动私有完成状态机。会与请求层重复并易不一致。

5. DMA 缓冲使用内核物理页，以物理地址提供给设备；virtqueue 环与 virtio cfg/MMIO 通过现有内核虚拟内存能力映射。
   - 原因：无 IOMMU 下设备按物理地址访问；显式物理地址与有界映射符合现有 MMIO 风格。
   - 备选：依赖 direct map 假设或引入 IOMMU。前者不可移植，后者超出首版。

6. IRQ-safe completion 入口只做有界工作：读 used ring、按 token 完成、唤醒等待者；不发送 PIC EOI（MSI-X 走 LAPIC EOI），不阻塞、不分配、不做 cache/filesystem policy。
   - 原因：与 `interrupt-driven-io-completion` 的 IRQ-safe 契约一致。
   - 备选：在 IRQ 内做 cache/FS 后续处理。违反 IRQ-safe 边界。

7. 驱动以内核内部稳定角色发布，不改变默认启动设备。
   - 原因：首版聚焦能力验证；默认启动仍走 ATA/exFAT，避免引入启动回归。
   - 备选：直接挂为默认 FS 后端。属于后续集成变更范围。

## Risks / Trade-offs

- [Risk] modern virtio capability 布局或 BAR 在不同 QEMU 版本差异。→ Mitigation：动态解析 capability/BAR 而非写死；不满足时返回确定性错误并记录跳过。
- [Risk] feature 协商（VIRTIO_F_VERSION_1 等）失败导致设备不可用。→ Mitigation：严格按 virtio status 步骤推进，失败置 FAILED status 并诊断。
- [Risk] virtqueue 物理地址/对齐错误导致设备访问错误内存。→ Mitigation：使用对齐内核物理页，校验 descriptor 物理地址；验证读写往返一致性。
- [Risk] MSI-X used 通知与请求 token 不匹配或迟到完成。→ Mitigation：复用请求层 token/generation 校验，迟到/重复完成被拒绝或诊断。
- [Risk] IRQ 入口解析 used ring 耗时过长。→ Mitigation：单队列、有界批次处理，仅完成已 pending 请求并唤醒。
- [Risk] 真实设备时序与可控验证不同。→ Mitigation：依赖前置 MSI-X 能力已验证投递；本变更验证 virtio-blk 读写闭环，环境不可用时记录残余风险。

## Migration Plan

1. 梳理设备框架注册/probe、块请求层 issue/completion、MSI-X 向量绑定与内核虚拟内存映射接口。
2. 实现 virtio modern PCI transport：capability 解析、cfg 映射、reset/feature/status。
3. 初始化单 split virtqueue 与 DMA 缓冲（物理页 + 有界映射）。
4. 实现块请求 issue：构造 virtio-blk 请求头/数据/状态 descriptor，notify 设备，进入 pending。
5. 绑定 MSI-X 向量，IRQ-safe completion 入口解析 used ring，按 token 完成请求并唤醒等待者。
6. 通过设备框架以内核内部稳定角色发布该后端，不改变默认启动设备。
7. 增加默认关闭验证：发布、读写往返、错误/timeout、MSI-X 完成闭环、默认启动回归与跳过记录。
8. 回滚策略：移除 virtio-blk 驱动与验证开关即可，不涉及磁盘镜像、boot handoff、用户态 ABI 或默认启动设备改动。

## Open Questions

- 暂无。transport=modern-only、interrupt=MSI-X、queue=单 split ring 已确定；多队列/packed ring/默认 FS 后端集成留作后续变更。
