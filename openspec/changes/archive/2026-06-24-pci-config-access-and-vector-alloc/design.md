## Context

BigOS 当前中断基础已经具备：内核拥有的静态 IDT、稳定的 `InterruptFrame` 分发 ABI、按 `VectorOwner` 区分异常/syscall/PIC/LAPIC/spurious 的分类、LAPIC（含 x2APIC）EOI 与 timer/IPI、IOAPIC redirection 路由。但外部中断向量目前以写死常量分配，例如 timer/keyboard 落在 i8259 remap 区间，LAPIC timer=0xef、scheduler nudge=0xee、TLB shootdown=0xed，syscall=0x80。内核没有 PCI 配置空间访问能力，也没有一个统一的“申请一个可用外部中断向量并注册 handler”的接口。

要接入现代 PCI 设备和 MSI-X，必须先补两块共享前置能力：一是有界 PCI 配置空间访问（读取配置头、遍历 capability list、读取 BAR 描述），二是有界内核中断向量分配/注册（在受限区间内动态分配向量，复用现有 ISR 注册和 LAPIC EOI 所有权）。本变更只提供这两块能力本身，不实现 MSI/MSI-X 编程，也不实现具体设备驱动。

本变更跨越 PCI IO 端口访问、IDT/向量分配、ISR 注册与 LAPIC EOI 边界。默认运行目标仍是 x86_64 Legacy BIOS/MBR/exFAT，QEMU 是主要验证环境。

## Goals / Non-Goals

**Goals:**

- 提供按 bus/device/function/offset 的 32 位对齐 PCI 配置空间读（必要时写）能力，基于传统 0xCF8/0xCFC 端口机制。
- 提供 vendor/device 探测、`capabilities pointer` 跟随、capability list 遍历和 BAR 原始值/类型（IO vs MMIO、32 vs 64 位、size 探测）读取。
- 提供从受限保留向量区间分配/释放一个可屏蔽外部中断向量，并通过现有 ISR 注册接口绑定 handler、标注 `VectorOwner::Lapic` 的能力。
- 保持配置访问与向量分配只在普通可阻塞内核上下文执行，向量注册产物与现有分发 ABI 和 EOI 所有权完全兼容。
- 提供默认关闭验证，覆盖设备探测、capability 遍历、BAR 读取、向量分配/释放/重复释放与耗尽边界。

**Non-Goals:**

- 不实现 ECAM/MMCONFIG、PCIe 扩展配置空间（offset >= 0x100）或 PCIe 扩展 capability。
- 不实现完整 PCI 总线枚举树、桥递归、资源重分配、热插拔或电源管理。
- 不实现 MSI/MSI-X capability 解析与 message 编程（属于后续 `pci-msix-interrupt-delivery`）。
- 不强制映射设备 MMIO（仅读取并返回 BAR 描述，实际映射由消费方按需进行）。
- 不改变现有写死向量、syscall 向量、异常向量、PIC fallback、IOAPIC 默认路由或 IRQ affinity 策略。
- 不引入用户可见设备节点、新 syscall ABI 或 SMP 向量迁移。

## Decisions

1. PCI 配置访问首版采用 0xCF8/0xCFC IO 端口机制，而不是 ECAM。
   - 原因：BigOS 当前为 Legacy BIOS 路径，QEMU 在该路径下稳定支持端口法；端口法实现量小、不依赖 ACPI MCFG 表解析。
   - 备选：直接做 ECAM/MMCONFIG。需要 ACPI MCFG 发现与 MMIO 映射，超出首版边界，留作后续。

2. 配置访问 API 以 32 位对齐 dword 读为基础，8/16 位访问在其上做掩码移位。
   - 原因：0xCF8/0xCFC 机制本质是 dword 访问；统一以 dword 为底层可避免对齐与跨边界歧义。
   - 备选：直接暴露任意宽度端口访问。容易在 offset 非对齐时产生未定义读，且不利于审查。

3. capability 遍历与 BAR 读取作为只读能力提供，不在本变更内映射 MMIO。
   - 原因：不同消费方（MSI-X table、virtio cfg）对映射粒度与时机要求不同；本变更只负责“描述”，映射决策交给消费方，避免过早绑定。
   - 备选：在 BAR 读取时顺带映射。会把内核虚拟内存映射策略和设备生命周期混入 PCI 访问层。

4. 向量分配复用现有静态 IDT、ISR 注册与 `VectorOwner`，只新增一个受限“动态外部中断向量区间”。
   - 原因：现有 EOI 所有权和分发 ABI 已稳定；MSI-X 中断在投递语义上等同 LAPIC 拥有 EOI 的固定向量，所以动态向量应标注 `VectorOwner::Lapic` 并走现有 LAPIC EOI 边界。
   - 备选：为 MSI-X 引入独立 EOI 路径。会破坏现有“EOI 所有权唯一”的不变量。

5. 动态向量区间从现有写死向量之外的安全区间选取，避免与异常(0x00-0x1f)、PIC remap(0x20-0x2f)、syscall(0x80)、现有 LAPIC 向量(0xed-0xef)冲突。
   - 原因：保证新增能力不触碰任何既有向量语义；区间大小有界且静态，便于审查耗尽行为。
   - 备选：在整个可用向量空间动态扫描。增加与未来固定向量冲突的风险，不利于确定性。

6. 配置访问与向量分配只允许普通可阻塞内核上下文调用。
   - 原因：探测/遍历涉及端口 IO 与潜在诊断输出；分配涉及共享表更新。这些都不应在 IRQ 上下文或抢占禁用区进行。
   - 备选：允许 IRQ 上下文分配向量。会引入重入与一致性风险。

## Risks / Trade-offs

- [Risk] 端口法 PCI 访问在某些平台/固件下行为不一致。→ Mitigation：首版以 QEMU Legacy BIOS 为主验证目标；不可用环境记录跳过与残余风险，不声明通用平台覆盖。
- [Risk] BAR size 探测需要写全 1 再回读，若不慎写到非 BAR 寄存器会破坏设备状态。→ Mitigation：严格限定 size 探测只作用于 BAR 偏移，并在写后恢复原值；该路径仅在初始化上下文执行。
- [Risk] 动态向量区间过小导致后续多设备/多队列耗尽。→ Mitigation：区间大小作为有界常量集中定义，分配耗尽返回确定性错误；后续可在不破坏 ABI 的前提下调整大小。
- [Risk] 新增向量与未来固定向量规划冲突。→ Mitigation：动态区间与所有现有固定向量集中登记并加注释，分配器拒绝越界；向量规划改动需显式审查。
- [Risk] 公共头膨胀。→ Mitigation：PCI 访问与向量分配各自最小公共头，仅暴露消费方必需接口，端口常量与内部表留在实现文件。

## Migration Plan

1. 梳理现有 IDT、ISR 注册、`VectorOwner`、LAPIC EOI 与现有写死向量集合，确定安全的动态向量区间。
2. 实现 PCI 配置访问：dword 读/写、8/16 位派生读、vendor/device 探测、capability list 遍历、BAR 原始值与类型/size 探测。
3. 实现向量分配：受限区间的分配/释放、与现有 ISR 注册绑定、标注 `VectorOwner::Lapic`、耗尽与重复释放的确定性错误。
4. 增加默认关闭验证入口：探测已知 QEMU PCI 设备、遍历其 capability、读取 BAR 描述、分配并释放一个向量、覆盖重复释放与耗尽边界。
5. 记录验证：通过项、因环境不可用跳过项与残余风险、历史诊断与本次引入诊断分离。
6. 回滚策略：移除新增 PCI 访问与向量分配模块及其验证开关即可，不涉及磁盘镜像、boot handoff、用户态 ABI 或既有向量改动。

## Open Questions

- 暂无。动态向量区间的具体起止值在实现时基于现有固定向量集合确定，并在公共头集中登记。
