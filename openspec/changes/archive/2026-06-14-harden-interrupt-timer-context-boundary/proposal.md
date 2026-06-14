## Why

当前中断、计时、上下文切换与调度路径已经支撑最小可用系统，但这些路径仍强依赖 x86_64 的 IDT、ISR frame、PIC/PIT、汇编上下文帧和 IRQ-return 抢占细节。现在需要在真实消费点收紧 architecture-specific 机制与 portable kernel policy 的边界，为后续 VM/user-entry 边界清理和更远期 backend 扩展准备更清晰的接口。

## What Changes

- 梳理并硬化中断分发、timer tick、上下文切换、scheduler preemption 之间的边界，使 portable 调度/计时策略不直接扩散新的 x86_64 机制。
- 明确 architecture-owned 的状态与动作：IDT/ISR frame、异常与 IRQ 入口、EOI、硬件 timer 编程、上下文切换汇编帧、IRQ-return reschedule glue。
- 明确 core-owned 的策略与动作：tick accounting、sleep/wait queue 语义、preemption-disable 约束、调度决策、线程状态转换与可观察行为验证。
- 保持当前单核执行模型和现有 Legacy BIOS/x86_64 runnable backend，不引入 SMP、新 ISA runtime parity、UEFI runtime parity 或新的通用硬件抽象层。
- 保持现有 interrupt vector、syscall vector、上下文切换 ABI、boot/linker 地址、磁盘布局和用户可见 syscall ABI 的兼容性；若实现中必须触碰这些假设，必须在设计与验证记录中显式说明。

## Capabilities

### New Capabilities

- `interrupt-timer-context-boundary`: 覆盖中断、计时、上下文切换与 scheduler-facing architecture 机制之间的边界契约，以及该契约下的可观察验证要求。

### Modified Capabilities

- `interrupt-exception-foundation`: 收紧异常/IRQ 分发与 EOI 所属边界，要求 portable policy 不绕过 architecture-owned interrupt entry/exit 约束。
- `timer-irq-runtime`: 收紧硬件 timer 编程、timer IRQ 入口和 core tick policy 的责任分离。
- `kernel-thread-scheduler`: 收紧 scheduler 与 architecture context switch / IRQ-return preemption glue 的边界，保持单核调度语义不变。

## Impact

- 影响子系统：`kernel/core/irq`、`kernel/core/timer`、`kernel/core/sched`、`kernel/drivers/irqchip`、`kernel/drivers/timer`、`include/irq`、相关 x86_64 interrupt/context assembly。
- 影响 API/ABI：目标是边界硬化与内部接口整理；不计划改变用户态 syscall ABI、interrupt vector 编号、context switch frame layout、boot handoff、linker address、page-table layout 或 disk image layout。
- 依赖与工具链假设：继续使用 freestanding C++17/C17、x86_64 assembly、xmake、`x86_64-elf-gcc` toolchain；运行时验证仍以当前 QEMU/Bochs Legacy BIOS 路径为准。
- Emulator 与硬件假设：当前 runnable backend 仍为单核 x86_64 Legacy BIOS/MBR/exFAT；PIT/i8259 行为保持现状，不把 APIC、HPET、SMP interrupt routing 或新 boot backend 纳入本变更。
- Non-goals：不实现 SMP、per-CPU 调度、TLB shootdown、APIC/IOAPIC、完整硬件 timer 抽象、新 ISA 后端、UEFI runtime parity、POSIX 进程模型扩展或用户态 ABI 扩展。
