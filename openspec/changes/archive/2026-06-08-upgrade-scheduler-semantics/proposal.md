## Why

BigOS 已完成单核协作式调度、阻塞/睡眠原语、PIT tick、IRQ/exception/syscall 分发和 smoke 级用户态闭环；继续推进进程生命周期、文件描述符或更复杂 I/O 前，需要先稳定调度器从“显式 yield”到“timer 驱动抢占”的语义边界。

阶段 11 聚焦升级 scheduler semantics，在保持现有 `InterruptFrame`、context-switch frame、i8259 EOI 顺序和单核边界的前提下，引入 time slice、preemption-disable/critical-section 规则、优先级预留点，以及可验证的 reschedule-on-IRQ-return 路径。

## What Changes

- 扩展调度策略：为现有单核 scheduler 增加 time slice accounting，并允许 runnable 线程在 tick 到期后被标记为需要重新调度。
- 引入抢占控制：定义 preemption-disable、scheduler critical section、interrupt-disabled 区域和 blocking-context guard 之间的关系，禁止在不可抢占区执行 IRQ-return switch 或 sleep。
- 增加 IRQ return 重调度：timer IRQ0 可通过有界 IRQ-safe hook 设置 reschedule intent，并在安全的 interrupt return 边界执行线程切换。
- 保留 ABI 边界：实现前审查并保持 `InterruptFrame` layout、generated ISR frame layout、context-switch callee-saved frame、idle thread ownership 和 syscall/exception/IRQ 分发约定。
- 增加优先级预留点：第一版可以是固定 round-robin 加 priority hook 或静态 priority 字段，但不要求完整多级队列、实时调度或公平性策略。
- 增加验证：扩展 scheduler/runtime smoke matrix，覆盖 cooperative yield、timer slice expiry、preemption-disable protection、IRQ EOI ordering 和 blocked/sleeping 线程不被错误调度。

## Capabilities

### New Capabilities

- 无。阶段 11 升级现有 scheduler/timer/interrupt 能力，不引入独立新子系统。

### Modified Capabilities

- `kernel-thread-scheduler`: 将调度器需求从纯协作式 round-robin 升级为单核、timer-driven、可禁用抢占的调度语义，同时保留 blocked/sleeping 跳过规则和 idle thread ownership。
- `timer-irq-runtime`: timer IRQ0 需求从只记录 bounded reschedule intent 升级为可触发安全的 IRQ-return preemption，同时保持 `timer::on_tick()` 所有权和 IRQ-safe 限制。
- `interrupt-exception-foundation`: 中断返回语义增加 scheduler 切换边界要求，明确外部 IRQ 的单次 EOI、`InterruptFrame` ABI、exception fatal path 和 syscall vector 不被破坏。
- `runtime-smoke-validation`: runtime smoke matrix 增加 scheduler semantics/preemption case，并记录 QEMU headless marker、Bochs 或 QEMU+Bochs 交叉验证建议、跳过原因和残余风险。

## Impact

- 受影响子系统：`kernel/core/sched`、`include/bigos/sched.h`、`include/bigos/thread.h`、`kernel/core/timer`、`include/bigos/timer.h`、`kernel/core/irq`、`include/irq/interrupt.h`、`kernel/core/sched/switch.s`、`kernel/core/irq/interrupt.s`、runtime smoke matrix 和相关测试/文档。
- 架构假设：仅 x86_64 单核 Legacy BIOS/MBR/exFAT 路径；不引入 SMP、IPI、per-CPU run queue、跨 CPU locking、TLB shootdown 或 UEFI/OVMF backend。
- 内存假设：IRQ path、preemption path 和 context switch path 不通过普通 allocator 分配/释放 scheduler 对象；线程栈、TCB、run queue 节点和 wait queue linkage 仍由非中断上下文或既有所有权管理。
- emulator 与工具链假设：继续使用 `xmake`、`x86_64-elf-*` 工具链和 QEMU headless serial-marker smoke；涉及 IRQ/timer/port-IO/context-switch 行为时，在可用环境下使用 Bochs 或 QEMU+Bochs 交叉验证。
- 磁盘/文件系统假设：不改变 raw disk image、MBR/exFAT 布局、ATA PIO 读路径、只读 filesystem 语义或 `/boot/user/init.elf` 打包流程。
- 非目标：不实现 SMP、完整 process lifecycle、PID table、`wait`/`exit`、general `exec argv/envp`、fd/VFS/page cache、VMA/demand paging/COW、userland libc、signals、实时调度、完整 priority scheduler 或 POSIX policy。
