## 1. Interrupt Ownership And Routing Model

- [x] 1.1 梳理现有 IDT vector、CPU exception、`int 0x80` syscall、legacy PIC IRQ、LAPIC timer、IPI 和 IOAPIC external IRQ 分类，形成实现内使用的 irqchip ownership 表。
- [x] 1.2 定义 APIC-backed IRQ、PIC fallback IRQ、CPU exception、syscall 和 IPI 的 EOI 所有权，保证每个 resumable interrupt completion 最多通过一个 owner 发送 EOI。
- [x] 1.3 调整 interrupt dispatch 边界，使 handler 继续接收稳定 `InterruptFrame`，同时根据 source/owner 选择 PIC EOI、LAPIC EOI 或 no-EOI。
- [x] 1.4 增加 unsupported/unregistered vector 的确定性诊断，输出 vector 和已知 owner 分类，避免未知 owner 路径发送错误 EOI。

## 2. LAPIC/IOAPIC Default Delivery

- [x] 2.1 完善 LAPIC enable、local APIC EOI、per-CPU interrupt state 和 APIC capability gating，确保 APIC active 前所有目标 CPU 状态已初始化。
- [x] 2.2 完善 IOAPIC redirection 配置边界，支持受控配置默认 external IRQ source 的 vector、mask、trigger/polarity、delivery target 和 owner，并通过内部 helper 选择默认 IRQ target。
- [x] 2.3 将 APIC-backed default path 与 legacy PIC fallback path 分离，禁止同一 active IRQ source 同时由 PIC 和 APIC 路径拥有。
- [x] 2.4 实现 APIC 不可用或拓扑不完整时的 deterministic fail-closed 或文档化 BSP-only PIC/PIT fallback，禁止半启用多核默认中断投递。

## 3. Default IRQ Source Migration

- [x] 3.1 将默认 scheduler tick ownership 收敛到 per-CPU LAPIC timer，保留 PIT 作为校准、诊断或 fallback 参考。
- [x] 3.2 将 keyboard/input IRQ 迁移到 IOAPIC 默认路由，第一版默认投递到 BSP，并通过内部 target 选择边界保留后续 service CPU / IRQ affinity 扩展空间。
- [x] 3.3 审查 scheduler nudge IPI 与 TLB shootdown IPI 的 vector 分类和 LAPIC EOI 路径，确认它们不与 external IRQ routing 或 syscall vector 混淆。
- [x] 3.4 审查 AP tick、keyboard IRQ、IPI 和 IRQ-return preemption 路径，确认不访问 BSP-only scheduler/timer 状态，也不在 hard IRQ context 执行阻塞 I/O 或可阻塞分配。

## 4. ABI And Layout Review

- [x] 4.1 评审并记录 syscall ABI、`int 0x80` vector、syscall 参数/返回、syscall no-EOI 语义是否保持不变。
- [x] 4.2 评审并记录进程/信号、time/timer 可见行为、resident init、shell 交互和 bounded userland baseline 是否保持兼容。
- [x] 4.3 评审并记录 kernel link address、AP trampoline 范围、boot handoff、GDT/TSS、page-table layout、direct map、page-table self-mapping、disk image layout 和 user address-space layout 是否保持不变。
- [x] 4.4 若发现任何用户可见 ABI 或 layout 变化，明确标记 breaking 风险并拆分或更新对应规格；若无变化，在验证记录中列出已审查边界。

## 5. Validation And Documentation

- [x] 5.1 增加或更新源码级检查，覆盖 vector 分类、irqchip ownership、EOI 唯一性、APIC fallback gating、hard IRQ non-blocking 和 target CPU online/initialized 约束。
- [x] 5.2 运行 `openspec validate enable-apic-default-interrupt-delivery --strict` 并修复当前 change 引入的规格问题。
- [x] 5.3 运行最窄可用 `xmake` 交叉编译；若 `x86_64-elf-gcc`、`x86_64-elf-g++`、`xmake` 或本地配置缺失，记录阻塞原因和残余风险。
- [x] 5.4 运行 C++ 辅助静态检查或 clang/clangd 等价诊断，并按 freestanding C++17、x86_64 target、no exceptions、no RTTI 和项目 include paths 尽量贴近 cross-build；记录历史诊断、当前 change 诊断、工具链 false positive 和配置差距。
- [x] 5.5 使用 QEMU 多核 headless smoke 验证 APIC-backed timer progress、支持的 external IRQ delivery 和 bounded userland baseline；若通过 Python helper 执行，使用 `uv run python ...`，若 `uv` 或 QEMU 不可用则记录跳过原因。
- [x] 5.6 可用时使用 Bochs 交叉验证 APIC/default IRQ 行为；若本地 Bochs 多核、ROM、display 或 APIC 配置不可用，记录 skipped checks 和残余风险。
- [x] 5.7 更新相关架构/验证文档时保持 `docs/en` 与 `docs/zh` 镜像同步，并避免声称 CPU hotplug、NUMA、MSI/MSI-X、完整 IRQ affinity、非 x86_64 backend parity 或 release-grade CI。
