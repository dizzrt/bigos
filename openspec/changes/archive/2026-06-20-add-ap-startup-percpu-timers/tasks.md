## 1. CPU 拓扑与启动边界

- [x] 1.1 审计当前 boot、IRQ、timer、scheduler 和 per-CPU preparation 调用点，确认 AP startup 不改变现有 BSP-only 默认语义。
- [x] 1.2 设计并实现有界 CPU table，记录 BSP/AP、APIC id、内部 CPU id、online/offline/failure 状态和 per-CPU timer 状态。
- [x] 1.3 基于 MP table 建立 CPU 枚举入口，处理 APIC id 映射、IOAPIC 描述、最大 CPU 数限制、重复 CPU、缺失拓扑信息和不支持硬件的确定性失败。
- [x] 1.4 将 current CPU 访问、BSP 状态和 AP 状态接入既有 SMP preparation per-CPU 边界，保留 bootstrap-only fallback。

## 2. AP Trampoline 与长模式进入

- [x] 2.1 确定 AP trampoline 低地址保留区，记录地址/大小/ownership，并防止 buddy/slab 或 boot asset 路径复用该区域。
- [x] 2.2 实现 AP trampoline 复制和启动 mailbox，传递 page-table root、GDT/TSS 信息、AP stack、entry point 和 CPU id。
- [x] 2.3 实现 AP 长模式入口，完成 per-CPU state 绑定、kernel stack、GDT/TSS、IDT 可用性、IRQ/preemption counters 初始化。
- [x] 2.4 实现 BSP 等待 AP online ack 的有界超时和失败路径，确保失败 AP 不会暴露给 scheduler、timer 或用户态路径。

## 3. APIC 与中断控制边界

- [x] 3.1 实现 LAPIC capability 检测、基础启用、寄存器访问封装和诊断状态，保持 freestanding-safe。
- [x] 3.2 实现 LAPIC INIT/SIPI 发送序列，用于启动已枚举 AP，并记录每个 AP 的启动结果。
- [x] 3.3 实现 LAPIC EOI 边界，区分 LAPIC-backed interrupt、legacy i8259 EOI 和 syscall/no-EOI 路径。
- [x] 3.4 增加 IOAPIC 初始化和 timer 相关路由描述边界，将调度 timer interrupt 迁移到 APIC-backed timer 路径。
- [x] 3.5 复核 interrupt vector、EOI ordering、syscall vector、exception dispatch、legacy PIC/PIT 校准路径和未迁移外部 IRQ，确认该 change 未引入隐式 ABI 改动。

## 4. Per-CPU Timer 基线

- [x] 4.1 使用 PIT 参考校准 BSP local APIC timer，并定义从 legacy PIT/PIC tick 到 APIC-backed timer interrupt 的过渡策略。
- [x] 4.2 实现 AP local timer 初始化，确保 AP online 后才能接收并统计有效 per-CPU timer tick。
- [x] 4.3 将 per-CPU timer tick 接入现有 timer/scheduler 非阻塞边界，禁止 AP tick 执行阻塞 I/O、文件系统访问或用户态路径。
- [x] 4.4 在 per-CPU run queue 尚未实现前，确保 AP timer tick 不迁移 runnable work、不执行跨 CPU wakeup、不声称调度吞吐扩展。

## 5. Failure、诊断与默认配置

- [x] 5.1 为 MP table 不可用或无效、APIC 不可用、CPU 枚举失败、AP 超时、trampoline 保留冲突、PIT 参考校准失败、timer 初始化失败和容量耗尽提供确定性诊断。
- [x] 5.2 提供 AP startup/per-CPU timer 的默认关闭或可回退配置，使不支持 APIC 的环境可继续 BSP-only 启动。
- [x] 5.3 确认 panic/serial/VGA 诊断不会在 AP 半初始化、不可阻塞 IRQ 上下文或错误 CR3 下访问不安全状态。
- [x] 5.4 更新相关 OpenSpec validation notes 或实现说明，明确该 change 不包含 per-CPU run queue、IPI TLB shootdown、CPU hotplug 或完整 APIC 默认中断投递。

## 6. 构建、静态检查与运行验证

- [x] 6.1 运行 OpenSpec 状态和严格校验，确认 proposal、design、specs 和 tasks 均可解析。
- [x] 6.2 运行 xmake 或等价 x86_64 cross-toolchain 窄构建；若 `x86_64-elf-gcc`/xmake 不可用，记录缺失工具和残余风险。
- [x] 6.3 对新增或修改的 C/C++/assembly 入口执行 clang/clangd 辅助诊断，按 freestanding C++17、x86_64、no exceptions、no RTTI 约束配置；无法等价配置时记录差距。
- [x] 6.4 使用 QEMU headless 多核配置运行 bounded AP startup/per-CPU timer smoke，观察 AP online ack、per-CPU tick progress 和 bounded userland baseline 未回退。
- [x] 6.5 如本地支持，使用 Bochs 或第二 QEMU 配置交叉验证 APIC/trampoline 行为；不可用时记录跳过原因和硬件行为残余风险。
- [x] 6.6 执行 targeted consistency search，确认 roadmap 和文档未把该 change 描述为完整 SMP、跨 CPU 调度、IPI shootdown 或广泛 APIC interrupt parity。
