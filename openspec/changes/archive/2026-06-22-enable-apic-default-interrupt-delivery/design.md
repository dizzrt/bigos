## Context

BigOS 当前多核相关能力已经覆盖 AP startup、per-CPU local timer、per-CPU run queue、scheduler nudge、typed IPI 和 TLB shootdown。剩余缺口是默认外部中断投递仍未成为 APIC-backed 运行时路径：部分能力已经依赖 LAPIC/IOAPIC，但现有边界仍允许把“完整 APIC 默认中断投递”作为未来事项。

本 change 跨越 x86_64 IRQ dispatch、irqchip、LAPIC/IOAPIC、timer、keyboard/input、scheduler IRQ-return preemption 和 SMP IPI 边界。实现必须保持 freestanding-safe，继续以当前 x86_64 legacy boot/storage path 为交付基线；不改变 kernel link address、AP trampoline 固定低地址区域、IDT/syscall ABI、disk layout、page-table layout、direct map、page-table self-mapping 或用户态 ABI。

## Goals / Non-Goals

**Goals:**

- 让 x86_64 默认运行时外部 IRQ delivery 使用 APIC-backed 路径，而不是把 APIC 只作为 AP startup、local timer 或 IPI 的局部能力。
- 建立统一 irqchip ownership：CPU exception 与 syscall 不走 irqchip EOI；legacy PIC fallback 只由 PIC EOI；APIC-backed IRQ、timer 和 IPI 只由 LAPIC/IOAPIC 边界负责 EOI/routing。
- 将已支持的默认 IRQ 源纳入 APIC-backed delivery，至少覆盖 scheduler tick 和 keyboard/input IRQ；尚未支持的设备源必须保持未声明或 fallback 受控。
- 保留 APIC 不可用时的 deterministic failure 或 BSP-only 兼容/诊断路径，避免无界启动或半迁移状态。
- 完成用户可见 ABI 评审，确认 syscall ABI、进程/信号语义、用户地址空间、磁盘布局、boot handoff 和公开诊断标记是否保持兼容。
- 提供源码不变量检查、cross-toolchain build、clang/clangd 辅助诊断和 QEMU 多核 smoke 验证边界；Bochs 或本地 APIC 配置不可用时显式记录。

**Non-Goals:**

- 不新增 ISA、UEFI runtime parity、storage backend、virtio/AHCI/NVMe、PCI enumeration、MSI/MSI-X 或设备热插拔。
- 不实现 CPU hotplug、NUMA、完整 IRQ affinity/load balancing、电源管理或完整 ACPI 设备模型。
- 不改变 `int 0x80` syscall ABI、用户态 POSIX 子集、文件系统 ABI、磁盘镜像布局、kernel/user address layout 或 COW/demand paging 语义。
- 不把 APIC 默认投递验证等同于完整 release-grade CI、完整硬件兼容矩阵或非 x86_64 backend parity。

## Decisions

1. 默认中断投递采用 irqchip-owned routing，而不是在 dispatch 层散落 PIC/APIC 特判。

   Rationale: PIC fallback、APIC external IRQ、local timer 和 IPI 的 EOI 所有者不同。显式 irqchip ownership 可以让 dispatch 只根据 vector/source 分类选择 EOI 边界，避免双 EOI、漏 EOI 或 syscall/exception 误入 irqchip。

   Alternatives considered: 在各 handler 内手写 EOI。该方案会破坏现有“handler 不负责 EOI”的边界，也会让 keyboard/timer/IPI 迁移时出现重复 EOI 风险。

2. APIC-backed default path 以 IOAPIC redirection + LAPIC local delivery 为默认运行时路径，PIC 只作为受控 fallback。

   Rationale: 多核场景下外部 IRQ 需要明确投递到 BSP 或指定 online CPU，并通过 LAPIC 完成中断接收和 EOI。PIC 适合作为兼容路径，但不应继续作为多核默认投递模型。

   Alternatives considered: 保持 PIC 为默认，只让 AP 使用 LAPIC timer/IPI。该方案无法闭合默认中断模型，且会让多核 scheduler/userland baseline 与外部 IRQ ownership 分裂。

3. Timer ownership 优先使用 per-CPU LAPIC timer，PIT 保留为校准或 fallback 参考。

   Rationale: per-CPU run queue 已要求 CPU-local tick 和 IRQ-return preemption。默认调度 tick 需要属于当前 CPU 的 local timer；PIT/PIC tick 不能作为多核默认 scheduler tick owner。

   Alternatives considered: PIT 继续作为全局 tick，再由 scheduler 分发到各 CPU。该方案增加跨 CPU 调度耦合，不能表达每 CPU本地 preemption。

4. Keyboard/input IRQ 第一版通过 IOAPIC 默认路由到 BSP，并通过内部 target 选择边界保留后续扩展空间。

   Rationale: keyboard 是当前用户可见交互路径，需要纳入默认 APIC delivery；但完整 affinity、负载均衡和设备模型超出当前边界。BSP 初始化最早、现有输入与 legacy IRQ 路径最接近 BSP-only 假设，适合作为第一版低频交互 IRQ 的默认目标。实现上仍应封装 `select_default_irq_target` 一类内部边界，返回 online 且 per-CPU interrupt state 已初始化的 BSP；后续可在不重写 IOAPIC routing 主流程的前提下扩展为 service CPU 或 per-device affinity。

   Alternatives considered: 同时引入通用 IRQ affinity API。该方案会扩大设备框架和调度策略面，不是使默认 APIC delivery 闭合所必需。

5. APIC 不可用或拓扑不完整时 fail closed 或进入文档化 BSP-only fallback，不做静默半启用。

   Rationale: 半启用 APIC 可能导致 timer、keyboard、IPI 或 EOI 所有权不一致。失败路径必须可诊断，fallback 必须明确使用 PIC/PIT 兼容语义并禁止声明多核默认投递。

   Alternatives considered: 任意缺失时自动回到 PIC 并继续启用 AP。该方案会让多核状态和中断投递模型不一致，调试成本高。

6. ABI 评审作为实现任务和验证记录的一部分，而不是隐含在“无用户态改动”中。

   Rationale: IRQ routing 看似内核内部，但可能影响信号投递时序、timer 可见行为、shell 交互、诊断 marker 和启动配置。显式评审可以把“无 breaking ABI”变成可审查结论。

   Alternatives considered: 只跑 userland smoke 后默认 ABI 未变。该方案不足以覆盖 boot handoff、syscall vector、diagnostic marker 和磁盘布局等边界。

## Risks / Trade-offs

- [Risk] PIC 与 APIC 路径重复 EOI 或漏 EOI -> Mitigation: 引入 irqchip ownership 分类，源码检查覆盖 exception/syscall/no-EOI、PIC EOI、LAPIC EOI 三类路径。
- [Risk] IOAPIC redirection 配置错误导致 keyboard 或 timer 丢中断 -> Mitigation: 分阶段迁移 timer 与 keyboard，使用 QEMU 多核 smoke 观察 tick、输入路径和 bounded userland baseline。
- [Risk] APIC 不可用时 fallback 语义不清 -> Mitigation: 初始化阶段记录 APIC capability/topology 状态；缺失时 fail closed 或明确 BSP-only PIC/PIT fallback，并禁止 AP 参与默认多核运行。
- [Risk] 中断投递目标与 scheduler/per-CPU state 不一致 -> Mitigation: 默认 IRQ target 只选择 online 且拥有初始化 per-CPU interrupt state 的 CPU；不支持的 target 进入 deterministic diagnostic。
- [Risk] ABI 影响被低估 -> Mitigation: 任务中单列 ABI review，覆盖 syscall vector、用户态进程/信号、timer/time 行为、shell 交互、boot/disk/layout 和诊断 marker。
- [Risk] Bochs 多核/APIC 支持受本地构建限制 -> Mitigation: QEMU 作为首选多核验证；Bochs 只在可用时交叉验证，缺失时记录工具链和残余风险。

## Migration Plan

1. 梳理当前 vector/source 分类，建立 APIC-backed IRQ、legacy PIC IRQ、CPU exception、syscall、IPI 的 ownership 表。
2. 完善 LAPIC/IOAPIC 初始化与 redirection 边界，使默认 external IRQ delivery 可以在 APIC 可用时发布为 active。
3. 将 scheduler tick 迁移到 per-CPU LAPIC timer 默认路径，保留 PIT 校准/fallback 语义。
4. 将 keyboard/input IRQ 迁移到 IOAPIC 默认路由，第一版默认投递到 BSP，并保持 handler、TTY handoff 和 EOI 边界不变。
5. 清理 legacy PIC 路径，使其只在 fallback 配置中负责 PIC remap/mask/EOI，不与 APIC active path 混用。
6. 完成用户可见 ABI review，并把结论写入实现验证记录。
7. 运行规格校验、cross-toolchain build、clang/clangd 辅助检查和 QEMU 多核 smoke；可用时补充 Bochs 交叉验证。

Rollback 策略：保留编译期或启动期 fallback gate。若 APIC-backed default path 失败，则禁止 AP 参与默认多核运行并退回 BSP-only PIC/PIT 兼容路径；若 fallback 也不满足安全条件，则 fail closed 并输出确定性诊断。

## Open Questions

- 无。keyboard/input IRQ 的第一版默认目标已确定为 BSP；后续 service CPU 或 IRQ affinity 属于后续扩展，不属于本 change 的实现范围。
