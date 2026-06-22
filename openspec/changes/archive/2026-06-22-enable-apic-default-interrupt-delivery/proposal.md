## Why

BigOS 已经具备 AP 启动、per-CPU run queue、IPI 与跨核 TLB shootdown 的基础，默认中断投递仍需要从 legacy PIC/PIT 兼容路径收敛到 APIC-backed 路径，才能使真实多核运行的中断模型闭合。

这项变更聚焦 x86_64 默认运行时中断投递语义：明确外部 IRQ、timer、IPI、EOI 与 fallback 的边界，并评审是否存在用户可见 ABI 变化。

## What Changes

- 将 x86_64 默认外部中断投递切换为 APIC-backed 路径，使用 LAPIC/IOAPIC 边界承载默认运行时 IRQ delivery。
- 保留受控 legacy fallback：当 APIC 拓扑、LAPIC 或 IOAPIC 初始化不可用时，系统必须 fail closed 或进入文档化的 BSP-only 诊断/兼容路径。
- 明确 PIC 与 APIC 的 EOI 所有权，避免同一外部中断在两个 irqchip 路径重复 EOI。
- 将默认 scheduler tick、keyboard/input IRQ 等已支持外部中断源纳入 APIC-backed 默认投递策略，同时保持 syscall vector 与 CPU exception 语义不变。
- 评审用户可见 ABI，包括 syscall ABI、进程/信号语义、用户地址空间布局、磁盘布局、boot handoff 与已公开诊断标记；预期不引入 **BREAKING** 用户 ABI 变化。
- 记录验证边界：x86_64 cross-toolchain build、IRQ/APIC 源级检查、QEMU 多核 smoke，Bochs 或本地 APIC 配置不可用时记录跳过原因和残余风险。

非目标：

- 不新增 ISA、storage backend、UEFI runtime parity 或设备热插拔能力。
- 不实现完整 ACPI/PCI 设备发现、电源管理、MSI/MSI-X、CPU hotplug、NUMA 或通用 IRQ affinity/load balancing。
- 不改变 `int 0x80` syscall ABI、用户态 libc/POSIX 子集、文件系统 ABI、磁盘镜像布局、kernel link address 或用户地址空间布局。

假设：

- 架构目标仍为当前 x86_64 runnable backend；legacy boot/storage path 仍是默认交付基线。
- 既有 AP startup、per-CPU timer、per-CPU scheduler、typed IPI 与 TLB shootdown 边界已经存在或可作为依赖边界使用。
- APIC MMIO/MSR 访问、IDT vector 分配、低地址 AP trampoline、kernel higher-half address、direct map 与 page-table self-mapping 地址不因本变更静默改变。
- 验证优先使用 `xmake`、`x86_64-elf-gcc`/`x86_64-elf-g++`、QEMU 多核配置；Bochs 支持视本地构建与 ROM/display 能力记录。

## Capabilities

### New Capabilities

- `apic-default-interrupt-delivery`: 覆盖 x86_64 默认 APIC-backed 外部中断投递、EOI 所有权、legacy fallback、已支持 IRQ 源迁移、ABI 评审和验证边界。

### Modified Capabilities

- `interrupt-exception-foundation`: 将默认外部 IRQ 分发从 PIC-only 基线扩展为可由 APIC irqchip 拥有，同时保持 CPU exception、syscall vector 与 ISR ABI 稳定。
- `ap-startup-percpu-timers`: 将此前只要求 timer/AP startup 所需 APIC 边界的限制，收敛为默认运行时 APIC-backed interrupt delivery 边界。
- `smp-preparation`: 将“完整 APIC 默认中断投递仍是未来依赖”的准备性表述，更新为本 change 负责激活的有界能力。

## Impact

- 影响子系统：x86_64 IRQ/IDT dispatch、irqchip 驱动、LAPIC/IOAPIC 初始化、timer ownership、keyboard/input IRQ routing、scheduler tick/IRQ-return preemption、SMP IPI 边界、启动初始化顺序。
- 影响 API/ABI：内核内部 irqchip/interrupt routing API 可能调整；用户可见 ABI 需要显式评审，预期保持兼容。
- 影响依赖：需要当前 x86_64 APIC 拓扑发现、LAPIC/IOAPIC 初始化、per-CPU state、typed IPI、per-CPU scheduler/timer 作为实现边界。
- 影响验证：需要 cross-toolchain build、clang/clangd 辅助诊断、源级 IRQ invariant 检查、QEMU 多核 smoke，并在缺少 Bochs/APIC 环境时记录不可运行项。
