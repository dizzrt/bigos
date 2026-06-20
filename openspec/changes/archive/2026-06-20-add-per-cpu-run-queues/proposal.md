## Why

BigOS 已具备 AP startup 与 per-CPU local timer 基线，但调度器仍停留在 BSP 单队列模型，AP 只能作为受控启动和计时器验证对象，不能承担普通 runnable work。需要引入受限的 per-CPU run queue 与跨 CPU 唤醒能力，让已上线 CPU 能参与内核线程调度，并为后续 IPI、TLB shootdown 与默认 APIC interrupt delivery 奠定可验证基础。

## What Changes

- 将调度器从 BSP-only runnable queue 扩展为 bounded per-CPU run queue，每个 online CPU 拥有当前线程、idle 线程、pending reschedule 与本地 runnable 队列。
- 增加受限的跨 CPU 调度选择：新建线程、显式 yield、blocking wakeup、timeout wakeup 和 timer tick 可在明确锁边界内选择本地或远端 CPU 的 runnable queue。
- 增加远程唤醒与 reschedule 请求边界，使唤醒目标 CPU 能从 idle 或当前线程时间片中断点观察到 runnable work；IPI 可作为实现机制，但本 change 不要求完整 IPI 子系统覆盖 TLB shootdown。
- 调整 AP startup/per-CPU timer 基线中“AP tick 不迁移 runnable work”的限制，使 AP 在 per-CPU run queue 启用后可以参与受控调度。
- 保持 x86_64 Legacy BIOS 默认交付目标、现有 boot 地址、trampoline 低地址范围、linker 地址、IDT/syscall ABI、磁盘布局和用户态 ABI 不变。
- 明确非目标：不实现 CPU hotplug、NUMA、完整负载均衡策略、实时调度、POSIX scheduling policy、跨 CPU TLB shootdown、完整 APIC-backed 默认外部中断投递、UEFI runtime parity 或新 ISA/backend。

## Capabilities

### New Capabilities

- `per-cpu-run-queues`: 描述 per-CPU runnable queue、CPU-local idle/current ownership、跨 CPU enqueue/wakeup、远程 reschedule 请求和多核调度验证边界。

### Modified Capabilities

- `kernel-thread-scheduler`: 将既有单核 round-robin 调度需求扩展为受限 SMP 调度；保留 bounded thread lifecycle、blocking/sleeping 状态、timer-driven preemption 与 context-switch ABI 约束。
- `smp-preparation`: 将 cross-CPU scheduling 与 remote wakeup 从“未来依赖”提升为本 change 明确启用的受控能力，同时保留 TLB shootdown 和完整 APIC 默认中断投递为后续能力。
- `ap-startup-percpu-timers`: 调整 AP local timer 场景，使 per-CPU run queue 启用后 AP tick 可以驱动本 CPU 调度，而不再仅限 idle/timer 验证。

## Impact

- 影响子系统：`kernel/core/sched`、`kernel/core/timer`、`kernel/core/irq`、`kernel/core/bigos/percpu`、x86_64 LAPIC/IPI 边界，以及相关 public headers。
- 架构假设：当前实现目标仍是 x86_64；依赖已上线 CPU 拓扑、AP startup、LAPIC EOI 与 per-CPU local timer 基线；不改变 AP trampoline 固定低地址区域。
- 内存与锁假设：run queue、wait queue、sleep list、current/idle ownership 和 remote wakeup 状态必须使用 IRQ-safe、SMP-aware 的调度器锁或等价边界保护；IRQ context 不得执行可阻塞操作或普通动态分配。
- 工具与验证假设：优先使用 QEMU 多核 headless smoke；Bochs 多核能力依赖本地构建，缺失时记录跳过项和残余风险；源码检查和 OpenSpec strict validation 必须覆盖新调度边界。
