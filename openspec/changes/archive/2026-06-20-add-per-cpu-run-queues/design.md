## Context

当前调度器实现以 `SchedulerCpuState g_boot_cpu_sched` 承载单一 runnable queue、sleep list、terminated list、current/idle thread 和 reschedule intent。AP startup 与 per-CPU local timer 已经能让 AP 进入内核并更新 CPU-local timer state，但 AP tick 不能调度普通 work，IRQ return preemption 也需要避免 AP 访问 BSP 专用调度状态。

本 change 跨越 scheduler、per-CPU state、timer IRQ、LAPIC/IPI 边界和验证脚本。设计目标是在不改变 boot/layout/ABI 的前提下，把现有单核调度状态拆成每个 online CPU 的调度域，并提供最小的跨 CPU enqueue 与 wakeup 机制。x86_64 Legacy BIOS 仍是默认交付目标；UEFI backend spike、新 ISA、完整 APIC external IRQ migration 和 TLB shootdown 不进入本 change。

## Goals / Non-Goals

**Goals:**

- 为每个 online CPU 提供 bounded scheduler state：current、idle、run queue、sleep list、reschedule pending、preemption/critical counters 和统计状态。
- 让新建线程、yield、wait queue wakeup、timeout wakeup、timer tick 和 IRQ-return preemption 使用 CPU-local scheduler state，而不是 BSP 全局状态。
- 提供受控 CPU placement：优先本地 CPU，允许在 online CPU 集合中选择目标 CPU，并支持远端 enqueue 后的 reschedule 请求。
- 提供远程唤醒边界：当目标 CPU idle 或需要尽快观察 runnable work 时，使用 LAPIC IPI 或等价 nudge 机制触发目标 CPU 重新评估调度。
- 保持 scheduler critical section、wait queue、sleep list、context switch、timer IRQ 和 blocking guard 的 IRQ-safe/SMP-aware 约束。
- 提供源码检查、OpenSpec strict validation、QEMU 多核 smoke 和可用时的 Bochs 交叉验证记录。

**Non-Goals:**

- 不实现 CPU hotplug、NUMA、CPU affinity API、完整 load balancing、work stealing、CFS、实时调度或 POSIX scheduling policy。
- 不实现跨 CPU TLB shootdown、完整 IPI 子系统、完整 APIC-backed external IRQ 默认投递或所有 legacy IRQ 迁移。
- 不改变 AP trampoline 固定低地址区域、kernel link address、IDT/syscall vector、interrupt frame ABI、context-switch frame ABI、磁盘布局或用户态 ABI。
- 不引入 hosted runtime、异常、RTTI、线程库、动态链接或外部依赖。

## Decisions

1. **调度状态从 BSP singleton 迁移到 per-CPU scheduler domain。**

   每个 online CPU 拥有独立 run queue、sleep list、current/idle thread、reschedule state 和本地统计。现有 `bigos::cpu::LocalState` 继续作为 current thread/process/address-space/preemption 等 CPU-local 观测边界，scheduler 内部增加更完整的 CPU-local scheduler state。

   备选方案是保留一个全局 run queue，让所有 CPU 竞争同一队列。该方案初期代码更少，但会把所有 context switch、timer tick、wakeup 和 idle 唤醒都压到同一锁上，且更容易复用 BSP-only 隐含状态；不适合作为后续 SMP 扩展基础。

2. **先实现显式 placement，暂不实现复杂负载均衡。**

   新建线程和 wakeup 可以选择当前 CPU 或一个 online CPU 中的确定性目标；策略只需保证 bounded、可验证和不会把 work 投递到 offline/failed CPU。后续可在同一边界内增加负载均衡或 affinity。

   备选方案是立即实现 periodic load balancing 或 work stealing。该方案会扩大锁顺序、跨 CPU 队列扫描和公平性问题，不符合当前受控启用目标。

3. **远程唤醒使用独立 reschedule request 边界。**

   远端 enqueue 后必须发布 runnable state，再设置目标 CPU 的 reschedule pending，并在需要时通过 LAPIC IPI 或等价 nudge 唤醒目标 CPU。该边界只服务调度，不承诺 TLB shootdown acknowledgement、泛化 IPI routing 或完整 interrupt delivery policy。

   备选方案是只依赖目标 CPU 的 local timer tick 发现 work。该方案实现简单但 wakeup latency 不可控，idle CPU 可能在 `hlt` 中等待下一次 tick，不满足跨 CPU wakeup 目标。

4. **锁顺序以 CPU id 为全局顺序。**

   单队列操作只持有目标 CPU run queue lock。需要同时移动两个 CPU 的状态时，按 CPU id 升序获取锁，并禁止在锁内执行阻塞 IO、普通动态分配、bulk console/serial 输出或进入 userland path。IRQ handler 只能执行 allocation-free 的 bounded enqueue/wakeup。

   备选方案是使用一个大 scheduler lock 包住所有 CPU 状态。该方案可降低死锁风险，但吞吐扩展价值很低，也会掩盖 per-CPU ownership 设计问题。

5. **保留 context-switch ABI，扩展调度前后的 CPU-local 发布。**

   `switch_kernel_context()` 与 interrupt frame ABI 不因本 change 改动。切换前后要更新 current thread、current process、address-space root、TSS/RSP0 和 reschedule flags 的 CPU-local ownership；用户进程上下文仍通过既有 proc/scheduler 边界恢复。

## Risks / Trade-offs

- [Risk] 远程 enqueue 与目标 CPU 观察 runnable state 的内存顺序错误可能导致丢失唤醒。→ Mitigation: enqueue、reschedule flag 和 IPI/nudge 之间使用明确的锁/atomic/fence 顺序；源码检查覆盖先发布 runnable 再通知目标 CPU。
- [Risk] AP timer 或 IPI handler 误入 BSP-only scheduler state 会复现 AP page fault/triple-fault 风险。→ Mitigation: 所有 tick、IRQ-return preemption 和 wakeup path 从 `current_cpu_id()` 获取 CPU-local scheduler domain，禁止直接访问 BSP 调度 singleton。
- [Risk] 多 CPU run queue 锁顺序错误导致死锁。→ Mitigation: 文档化 CPU id 升序锁顺序，迁移/跨队列操作封装为少数 helper，并用源码检查覆盖锁顺序和 IRQ context 限制。
- [Risk] 初版 placement 策略可能不公平。→ Mitigation: 本 change 只承诺 bounded cross-CPU scheduling 和 wakeup，不承诺完整公平性；统计信息和策略 hook 为后续负载均衡保留边界。
- [Risk] Bochs 多核能力在本地环境不可用或不稳定。→ Mitigation: QEMU 多核 headless smoke 是首选 runtime 证据；Bochs 仅在工具链支持时交叉验证，缺失时记录跳过项和残余风险。

## Migration Plan

1. 引入 per-CPU scheduler state，并让 BSP-only 配置继续退化为单 CPU 调度。
2. 将 create/yield/sleep/wakeup/tick/IRQ-return preemption 逐步切到 CPU-local state。
3. 增加远端 enqueue 与 reschedule request 边界，再启用 AP 上普通 runnable work。
4. 增加多核 scheduler smoke 和源码检查；确认普通 userland baseline 仍可启动。
5. 若 runtime smoke 暴露 AP 或 scheduler 回归，可通过 build option 暂时回退到 BSP-only scheduler path，并保留 AP startup/per-CPU timer baseline 验证。

## Resolved Decisions

- 初版 placement 采用“优先当前 CPU + 最短队列回退”：默认选择当前 CPU，当前 CPU 队列较长或远端 CPU 更适合接收 work 时，再在 online/schedulable CPU 中按 bounded queue depth 选择最短队列。队列长度可使用受锁保护或近似统计，不要求为初版负载均衡引入强一致全局扫描。
- 远程 reschedule 采用方案 B：复用现有 LAPIC IPI 底层发送 helper 作为 transport，但为 scheduler nudge 分配独立 interrupt vector。该 vector 的 handler 只服务调度唤醒语义，触发目标 CPU 在 IRQ-return 边界观察 reschedule pending / run queue；它不承诺 generic IPI、TLB shootdown、CPU hotplug 或完整 APIC default interrupt delivery。
