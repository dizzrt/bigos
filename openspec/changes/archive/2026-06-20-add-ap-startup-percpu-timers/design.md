## Context

BigOS 当前默认运行路径仍是 x86_64 Legacy BIOS/MBR/exFAT，已经具备单核 scheduler、IRQ-return timer preemption、SMP preparation 边界、设备/块层基础和 bounded userland。现有 SMP preparation 已经要求 per-CPU 状态、IRQ-safe locking、TLB invalidation 和 memory ordering 显式化，但真实 application processor 尚未启动，APIC/IOAPIC/per-CPU timer 也仍是未来依赖。

本 change 跨越早期 boot/arch、CPU 枚举、低地址 trampoline、页表/GDT/TSS、IRQ controller、timer tick 和 scheduler tick 入口。它需要把 AP bring-up 做成可诊断、可回退、可验证的硬件基础，同时避免一次性引入 per-CPU run queue、跨 CPU wakeup、IPI shootdown 和完整 APIC 默认中断路由。

关键约束：
- 目标 ISA 仍为 x86_64，不新增非 x86 backend，不要求 UEFI runtime parity。
- 不改变内核高半区链接地址、syscall ABI、IDT vector 语义、用户/内核 CR3 切换、磁盘布局或 bounded userland 行为。
- AP 启动所需低地址 trampoline 必须使用显式保留和复制边界，不能污染 buddy/slab 正常分配语义。
- AP 上线前不得运行用户态、文件系统、阻塞 I/O 或普通 scheduler work；AP 只能进入受控 idle/timer 验证路径。

## Goals / Non-Goals

**Goals:**

- 枚举并记录 BSP/AP CPU 拓扑，限制最大 CPU 数，并对不支持或超出容量的 CPU 给出确定性处理。
- 实现 AP startup：BSP 准备 trampoline 和 AP 启动参数，通过 LAPIC INIT/SIPI 启动 AP，AP 进入长模式后绑定 per-CPU 状态并确认 online。
- 建立 LAPIC/IOAPIC 初始化边界，支持 APIC-backed AP 启动和 per-CPU local timer，同时保留 legacy PIC/PIT 单核兼容路径。
- 为每个 online CPU 初始化 per-CPU timer，并把 tick 送入现有 timer/scheduler 边界中允许的非阻塞入口。
- 提供可验证的多核启动 smoke：证明 AP 上线、per-CPU tick 递增、BSP 不因 AP 启动破坏当前 userland baseline。
- 更新 SMP preparation 与 interrupt/timer 边界，使 APIC/per-CPU timer 成为受控启用能力。

**Non-Goals:**

- 不实现 per-CPU run queue、跨 CPU scheduling、load balancing、cross-CPU wakeup 或 work migration。
- 不实现 IPI-backed TLB shootdown、远端函数调用、CPU hotplug、NUMA、RCU 或完整内核抢占并行执行。
- 不把 APIC-backed external IRQ delivery 全面切为默认策略；除 AP startup 和 timer 所需范围外，legacy path 可继续服务默认单核交付。
- 不改变 syscall ABI、process/session/job-control 语义、dynamic linking、用户态 libc 能力或文件系统语义。
- 不新增 virtio、AHCI/SATA、NVMe、PCI/ACPI 完整枚举或新存储/设备 ISA。

## Decisions

1. AP 启动分为“发现、准备、发送、确认”四个阶段。

   BSP 先建立静态有界 CPU table，记录 CPU id、APIC id、state、stack、entry mailbox 和 timer 状态；随后准备 trampoline 和 AP 启动页表/GDT 信息；再通过 LAPIC 发送 INIT/SIPI；最后等待 AP 在长模式入口写入 online ack。备选方案是在 LAPIC 发送后直接假设 AP 已可用。该方案会让 AP 失败、栈错误和长模式进入失败无法诊断。

2. AP trampoline 使用显式低地址保留区。

   AP 从实模式或兼容启动状态进入 long mode，需要低地址 trampoline 和启动参数。实现应使用已有 boot memory/physical allocator 可保护的保留边界，并在 buddy/slab 正常管理前后保持 ownership 明确。备选方案是临时写入任意低地址页；该方案会破坏 boot asset、loader 数据或未来内存探测结果。

3. AP 上线后只进入受控 idle/timer 路径。

   本 change 的 AP 不参与普通 runnable work 分发。AP 初始化 per-CPU state、GDT/TSS、IDT 可用性和 LAPIC timer 后，只能进入 idle loop 或受控 tick smoke。备选方案是同时启用跨 CPU scheduler。该方案会把 AP bring-up、run queue 锁、IPI wakeup 和用户态并行执行耦合在一起，调试面过大。

4. LAPIC 是 AP 启动和 per-CPU timer 的必要边界，timer interrupt 迁移到 APIC-backed 路径。

   LAPIC 用于 AP INIT/SIPI、本地 APIC enable、EOI 和 local timer。第一版将调度 timer interrupt 从 legacy PIT/PIC tick 迁移到 APIC-backed timer 边界：PIT 保留为 LAPIC timer 校准参考，运行期调度 tick 由 BSP/AP 的 local APIC timer 提供；IOAPIC 同时建立 timer 相关路由和诊断边界。备选方案是只初始化 IOAPIC 而继续使用 legacy PIT/PIC timer interrupt；该方案降低改动风险，但不足以验证后续多核调度依赖的 APIC timer ownership。

5. per-CPU timer 先提供本地 tick，不提供完整跨核调度。

   每个 online CPU 的 local APIC timer 应能产生本地 tick，更新 CPU-local timer 计数，并通过现有 timer/scheduler 非阻塞边界表达“本 CPU 可调度 tick”。LAPIC timer 校准使用现有 PIT 作为参考源，避免依赖 CPUID/TSC 频率信息的可用性和稳定性；在没有 per-CPU run queue 前，该 tick 不迁移 work。备选方案是使用固定 emulator-friendly 初值；该方案实现更快，但会掩盖 timer calibration 和真实 APIC timer 行为问题。

6. CPU 枚举优先使用 MP table，并以 ACPI MADT 作为 fallback。

   当前默认 backend 仍是 Legacy BIOS，MP table 与当前启动路径匹配，因此作为第一优先拓扑来源；但默认 QEMU/SeaBIOS 多核配置可能只通过 ACPI MADT 暴露 CPU/LAPIC/IOAPIC 信息。为保证本 change 的 emulator-first 验证可重复，若 MP table 缺失或无效，内核应扫描 RSDP 并解析 RSDT/XSDT 中的 MADT 作为受控 fallback。该 fallback 只解析 AP startup 需要的 local APIC 与 IOAPIC 条目，不引入完整 ACPI 子系统、PCI/ACPI 设备枚举或非 Legacy backend parity。备选方案是只依赖 MP table；该方案实现更窄，但会让默认 QEMU 多核验证依赖脆弱的固件配置。

7. 失败策略 fail closed，默认单核路径可保留。

   CPU 枚举不可用、APIC 不支持、AP 启动超时、AP 数量超过容量、timer 校准失败或 AP ack 不一致时，内核必须进入诊断路径或明确回退到 BSP-only 运行，而不能把半初始化 AP 标记为 online。备选方案是忽略失败继续运行；这会隐藏中断路由和 per-CPU 状态损坏。

8. 验证优先使用 QEMU 多核 headless，Bochs/APIC 行为作为补充。

   QEMU 可用时，验证应覆盖 `-smp` 配置下 AP 上线和 per-CPU timer marker。Bochs 可用于早期 APIC/trampoline 行为交叉验证；若本地工具链或 emulator 不可用，validation notes 必须记录跳过项、替代检查和残余风险。

## Risks / Trade-offs

- [Risk] AP trampoline 低地址区域与 boot loader/allocator ownership 冲突。→ Mitigation: 使用显式保留区、记录地址和大小，加入启动期一致性检查，禁止 buddy/slab 回收该范围。
- [Risk] AP 进入长模式失败时系统看似挂起。→ Mitigation: BSP 等待带超时，AP 启动阶段写入可诊断状态，失败后 fail closed 或保留 BSP-only 路径。
- [Risk] PIT 参考校准和 LAPIC timer 迁移耦合后，早期 timer 失效会影响调度 tick。→ Mitigation: 保留有界校准超时和 BSP-only 诊断路径，校准失败不得发布有效 per-CPU timer。
- [Risk] timer interrupt 迁移过早影响 legacy PIC/PIT 路径。→ Mitigation: 本 change 只迁移调度 timer interrupt；键盘、ATA 和其他未显式迁移的 legacy external IRQ 继续保持当前路由，并通过 EOI ownership review 防止混用。
- [Risk] AP tick 进入 scheduler 后触发未准备好的跨 CPU 状态。→ Mitigation: AP tick 只进入 CPU-local 非阻塞路径，普通 runnable scheduling 仍受后续 per-CPU run queue change 保护。
- [Risk] 多核 emulator 行为与真实硬件差异较大。→ Mitigation: specs 只要求当前 x86_64 emulator 可验证的 bounded 行为，真实硬件 parity 和广泛设备支持不作为完成标准；MP table 与 ACPI MADT 均只作为拓扑来源，不扩大为完整 ACPI/硬件枚举承诺。

## Migration Plan

1. 基于 MP table 建立 CPU table、APIC id 枚举、IOAPIC 描述和 per-CPU bootstrap 状态；MP table 不可用时使用 ACPI MADT fallback，仍保留 BSP-only fail-closed fallback。
2. 增加 AP trampoline 低地址保留、复制和启动 mailbox，确保 boot memory ownership 可审计。
3. 实现 LAPIC 基础初始化、INIT/SIPI 发送和 AP online ack 等待，AP 长模式入口绑定 per-CPU state、stack、GDT/TSS 和 IDT。
4. 使用 PIT 参考校准 LAPIC local timer，增加 per-CPU tick 入口，并将调度 timer interrupt 迁移到 APIC-backed timer 路径。
5. 增加 IOAPIC 初始化和 timer 相关路由边界，但不强制替换键盘、ATA 等其他 legacy external IRQ。
6. 补充 AP startup/per-CPU timer smoke 和源码一致性检查，运行 OpenSpec 校验、窄构建和可用 emulator 验证。
7. 若 AP 启动或 local timer 不稳定，默认配置可关闭 AP bring-up，回到 BSP-only 路径；不需要用户态或磁盘格式迁移。

## Resolved Follow-Up Decisions

- 第一版 CPU 枚举优先使用 MP table；ACPI MADT 作为本 change 的 fallback 拓扑来源，仅解析 local APIC 与 IOAPIC，不引入完整 ACPI 子系统或设备枚举。
- LAPIC timer 校准使用 PIT 参考；CPUID/TSC 频率信息可作为后续优化或辅助诊断，不作为第一版正确性前提。
- 本 change 迁移调度 timer interrupt 到 APIC-backed timer 路径；IOAPIC 需要覆盖 timer 相关路由和诊断边界，但不要求迁移所有 legacy external IRQ。
