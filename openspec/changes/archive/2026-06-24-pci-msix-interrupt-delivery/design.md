## Context

BigOS 已有 LAPIC（含 x2APIC）EOI/timer/IPI 与 IOAPIC redirection。本变更的前置 `pci-config-access-and-vector-alloc` 提供了 PCI 配置空间访问、capability list 遍历、BAR 描述读取，以及在受限区间内分配/注册内核中断向量（标注 `VectorOwner::Lapic`、复用现有分发与 EOI）的能力。

现代 PCI 设备（首版目标 virtio-blk）使用 MSI-X：设备在配置空间有一个 MSI-X capability，指向某个 BAR 内的 MSI-X table（每条目含 message address、message data、vector control/mask）与 PBA（pending bit array）。要让设备通过 MSI-X 投递中断，内核需要：解析 capability、映射 table/PBA、为每个使用的条目编程 message address（指向 LAPIC）/message data（指向已分配向量）、控制 per-vector mask 与 function-level enable。

本变更跨越 PCI 配置访问、MMIO 映射、IDT/向量、LAPIC EOI 边界。默认运行目标仍是 x86_64 Legacy BIOS，QEMU 是主要验证环境。本变更只提供 MSI-X 投递能力本身，不实现具体存储设备驱动。

## Goals / Non-Goals

**Goals:**

- 解析设备 MSI-X capability：table size、table BAR 索引与偏移、PBA BAR 索引与偏移、function-level enable 与 function mask 位。
- 通过现有内核虚拟内存能力按需建立 MSI-X table/PBA 的有界 MMIO 映射。
- 为使用的 MSI-X 条目编程 message address（指向 LAPIC，按目标 APIC ID）与 message data（指向 `kernel-irq-vector-allocation` 分配的向量），并设置/清除 per-vector mask。
- 提供 function-level MSI-X enable/disable 与 per-vector mask/unmask，保证中断在配置完成前保持屏蔽。
- MSI-X 向量复用现有 `InterruptFrame` 分发与 LAPIC EOI 所有权。
- 提供默认关闭验证：用可控测试设备或可控 producer 验证 message 编程、mask/unmask 与向量投递 handler 闭环。

**Non-Goals:**

- 不实现 MSI（非 MSI-X）capability。
- 不实现 IRQ affinity 动态迁移、SMP 中断目标重排、中断合并/限流。
- 不实现 IOMMU/中断重映射（interrupt remapping）。
- 不实现完整设备热插拔、电源管理或 MSI-X 条目的运行时动态扩缩策略（首版条目集合在初始化时确定）。
- 不引入用户可见中断 ABI 或新 syscall。
- 不实现具体 virtio-blk 或其他设备驱动（属于后续变更）。

## Decisions

1. 仅实现 MSI-X，不实现 MSI（非 X）。
   - 原因：现代设备（virtio modern、NVMe）首选 MSI-X；MSI-X 的 table 模型更清晰、per-vector mask 更可控，便于有界实现与验证。
   - 备选：先做 MSI 再做 MSI-X。MSI 的多向量对齐与 mask 语义反而更受限，对首版目标无收益。

2. MSI-X table/PBA 通过现有内核虚拟内存能力按需建立有界 MMIO 映射，而不是依赖 direct map 假设。
   - 原因：BAR 物理地址不保证落在 direct map 范围；显式映射可控且符合现有 MMIO 访问风格（参考 LAPIC/IOAPIC 映射方式）。
   - 备选：假设 BAR 可经 direct map 访问。不可移植，且可能在不同 QEMU 配置下失败。

3. message address 指向 LAPIC，message data 编码已分配的内核向量，向量由 `kernel-irq-vector-allocation` 提供。
   - 原因：MSI-X 在 x86 上本质是“向 LAPIC 地址写入携带向量的消息”，与现有 LAPIC EOI 所有权天然一致；向量分配与 EOI 复用既有边界，避免新建中断路径。
   - 备选：为 MSI-X 自建向量空间与 EOI。会破坏 EOI 所有权唯一不变量并与现有分发重复。

4. 配置顺序为：先 function-mask/per-vector mask 屏蔽，再编程 table 条目与映射，最后 enable 并按需 unmask。
   - 原因：避免在 message 未编程完成前设备投递中断造成不确定行为；mask-先行是 MSI-X 编程的安全顺序。
   - 备选：先 enable 再编程。可能在窗口内产生杂散中断。

5. 首版 MSI-X 条目集合在初始化时确定，目标 CPU 默认 BSP。
   - 原因：当前默认中断投递以 BSP 为首选目标，与 IOAPIC keyboard 路由一致；保持有界、确定。
   - 备选：首版即支持多 CPU 分布与运行时重定向。属于 affinity 范畴，留作后续。

6. 本变更只提供 MSI-X 投递能力与可控验证，不绑定任何真实设备驱动。
   - 原因：把 MSI-X 平台能力与具体设备解耦，失败时可单独定位 MSI-X 编程/投递问题。
   - 备选：直接在 virtio-blk 里顺手实现 MSI-X。会把 PCI capability、MSI-X table、virtqueue 与 completion 混在一起，难归因。

## Risks / Trade-offs

- [Risk] 不同 QEMU 配置下 MSI-X table 所在 BAR/偏移不同或设备不支持 MSI-X。→ Mitigation：通过 capability 与 BAR 描述动态解析而非写死；不支持时返回确定性错误并记录跳过。
- [Risk] message address/data 编码错误导致向量投递到错误 vector 或不投递。→ Mitigation：用可控测试设备/producer 验证“编程的向量 == 实际触发的 handler”，覆盖 mask/unmask 行为。
- [Risk] MMIO 映射权限/缓存属性不当导致访问异常或读写不一致。→ Mitigation：复用现有 MMIO 映射方式（不可缓存），失败返回确定性错误并诊断。
- [Risk] 配置窗口内杂散中断。→ Mitigation：严格 mask-先行配置顺序，编程完成后再 enable/unmask。
- [Risk] EOI 所有权被破坏。→ Mitigation：MSI-X 向量统一标注 `VectorOwner::Lapic`，复用现有 LAPIC EOI，源级检查 EOI 唯一性。
- [Risk] 真实设备投递时序与可控 producer 不同。→ Mitigation：首版用可控 producer 稳定 MSI-X 编程/投递路径；真实设备 MSI-X 在后续 virtio-blk 变更中验证，残余风险显式记录。

## Migration Plan

1. 梳理 `pci-config-access` 的 capability/BAR 接口与 `kernel-irq-vector-allocation` 的分配/注册接口，确认 MSI-X 所需输入。
2. 实现 MSI-X capability 解析：table size、table/PBA BAR 索引与偏移、enable/function-mask 位读写。
3. 实现 MSI-X table/PBA 的有界 MMIO 映射（复用现有内核虚拟内存映射方式）。
4. 实现条目编程：message address（LAPIC + 目标 APIC ID）、message data（已分配向量）、per-vector mask 设置/清除；实现 function-level enable/disable。
5. 按 mask-先行顺序整合：屏蔽 → 编程 → enable → 按需 unmask。
6. 增加默认关闭验证：可控测试设备/producer 触发 MSI-X 向量，验证 handler 执行、mask 抑制投递、unmask 恢复投递。
7. 记录验证：通过项、因环境不可用跳过项与残余风险、历史诊断与本次诊断分离。
8. 回滚策略：移除 MSI-X 模块与验证开关即可，不涉及磁盘镜像、boot handoff、用户态 ABI 或既有向量改动。

## Open Questions

- 暂无。目标 CPU 首版固定为 BSP；多 CPU/affinity 留作后续 affinity 能力。
