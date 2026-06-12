## Context

BigOS 当前调度器是单核协作式 round-robin：线程通过 `yield()` 或阻塞/唤醒边界显式切换；PIT IRQ0 只推进 monotonic tick、处理 timeout/wakeup 或记录 bounded scheduler intent；中断路径保持固定 `InterruptFrame` ABI、外部 IRQ 单次 EOI、CPU exception 不 EOI、`int 0x80` syscall gate DPL=3。阶段 10 已加入 blocked/sleeping 状态、wait queue、timeout wait 和 blocking-context guard，但仍明确不做 IRQ-return context switch。

阶段 11 位于 roadmap 的 scheduler semantics upgrade。它需要把 timer tick、scheduler policy、interrupt return 和 context-switch assembly 连接起来，但不能顺手扩大到 SMP、process lifecycle、VFS、VM 或 UEFI。最重要的约束是：preemption 只能发生在明确安全的返回边界，不能破坏 ISR frame、callee-saved switch frame、idle thread ownership、wait queue membership 或 EOI 顺序。

## Goals / Non-Goals

**Goals:**

- 在单核边界内加入 time slice accounting，并让 timer IRQ0 能触发可验证的 reschedule-on-IRQ-return。
- 定义 preemption-disable、scheduler critical section、interrupt-disabled 区域和 blocking guard 的统一规则。
- 保留 existing cooperative yield 行为，使显式 `yield()`、blocking wait、timeout wakeup 和 idle thread 继续可用。
- 为后续 process lifecycle 和 fd/VFS 提供稳定的抢占边界，而不是把这些子系统一并实现。
- 用源码级检查、构建检查、QEMU headless marker smoke，以及可用时的 Bochs 或 QEMU+Bochs 交叉验证覆盖 IRQ/timer/context-switch 风险。

**Non-Goals:**

- 不引入 SMP、IPI、per-CPU run queue、cross-CPU locking、TLB shootdown 或抢占式多核语义。
- 不实现完整 priority scheduler、实时调度、公平调度类、deadline scheduler 或 load balancing。
- 不实现 PID table、process parent/child、`wait`/`exit`、general `exec argv/envp`、fd table、VFS、page cache、VMA、demand paging、COW、signal 或 libc。
- 不改变 boot layout、kernel link address、IDT vector、syscall vector `0x80`、page-table layout、disk image layout 或现有 smoke marker 含义。
- 不让 CPU exception handler 变成恢复或调度路径；`#PF` 仍是 diagnostic-only fatal path。

## Decisions

### Decision: 抢占只在 IRQ return 安全边界执行

Timer IRQ0 仍先通过 `timer::on_tick()` 推进 tick，再调用 scheduler 的 IRQ-safe tick hook 递减当前线程 time slice 或设置 reschedule intent。真正的 context switch 不在任意 handler 中立即执行，而是在外部 IRQ dispatch 完成 handler、保持单次 EOI 语义后，且确认当前 context 可抢占时，走受控的 IRQ-return scheduling path。

选择原因：这能保持 timer tick ownership 和 EOI 边界清晰，同时避免在 keyboard、filesystem、TTY 或 wait queue 的任意中间状态抢占。实现时需要明确 ISR frame 和 switch frame 的桥接点，不能把 cooperative switch primitive 当作普通 C++ 调用随意复用。

替代方案：在 `timer::on_tick()` 内直接切换线程。该方案把 timer 子系统变成 scheduler owner，容易违反 `on_tick()` IRQ-safe contract，也更难保证 EOI 和 interrupt frame 恢复顺序，因此不采用。

### Decision: preemption-disable 是 scheduler critical section 的显式计数

新增或复用 per-current-thread/per-kernel 的 preemption disable depth，用于保护 run queue、wait queue、sleep list、current thread state、context switch preparation 和低层输出/诊断等短临界区。进入该区域后 timer IRQ 可以记录 reschedule pending，但不能执行 IRQ-return switch；离开最外层 disable 后，如果 pending 且当前线程仍 runnable，必须在确定性边界触发 schedule 或在下一次返回边界执行。

选择原因：阶段 10 已经要求 blocking wait 不能发生在 scheduler critical section 内；阶段 11 需要同一套规则保护抢占，避免 blocked/sleeping/terminated 状态转换被 timer tick 打断。

替代方案：只依赖 `cli/sti` 关闭中断。该方案简单但会扩大 interrupt latency，且无法表达“IRQ 可以到达但暂不抢占”的状态，不利于后续 syscall/process 边界扩展。

### Decision: 第一版 priority 只提供 hook 或静态字段

线程控制块可以增加 priority hook、静态 priority 字段或 reserved policy field，但默认策略仍是单核 round-robin + time slice。选择下一个线程时可先保持现有 FIFO runnable order；priority 行为如果实现，必须是 bounded、无动态分配、可被 smoke 或 source check 观察的最小策略。

选择原因：roadmap 要求 priorities or priority hooks，但 BigOS 还没有 process ownership、nice/rt policy 或用户态 API。保留 hook 能避免把 Stage 11 扩散成完整调度类设计。

替代方案：立即实现多级反馈队列或实时优先级。该方案引入复杂公平性、饥饿和 priority inversion 问题，当前内核缺少锁、process 和 metrics 支撑，因此不采用。

### Decision: idle thread 继续拥有 halt 行为

抢占后 idle thread 仍是 scheduler-owned thread。无 runnable 非 idle 线程时运行 idle；idle 可在 documented interrupt-ready 状态下执行 `hlt`，由 timer/keyboard IRQ 唤醒 CPU。timer tick 不能把 idle 视作普通可消耗 time slice 的工作线程，也不能在没有其他 runnable work 时反复产生无意义 reschedule。

选择原因：保持现有 idle ownership 能防止回退到 unmanaged `kernel()` halt loop，也让 blocking/sleeping timeout 的唤醒路径保持单一。

替代方案：在无 runnable 线程时从 interrupt return 直接回到内核 halt loop。该方案会绕过 scheduler 状态机并破坏后续 process lifecycle 的统一调度入口，因此不采用。

### Decision: syscall 和 exception path 默认不可抢占

CPU exception handler、fatal diagnostic path 和 syscall dispatch 内部默认视为不可抢占上下文。未来若要让 syscall handler 可睡眠或可抢占，需要在 process lifecycle、user copy、fd table、退出/取消语义稳定后逐个开放。本阶段只允许在普通 kernel thread execution 和经过 guard 检查的 IRQ-return 边界抢占。

选择原因：当前 `int 0x80` 仍经 interrupt frame 进入，proc/user path 仍为 smoke-only。过早允许 syscall 内部睡眠/抢占会把 CR3、user buffer、exit/reap 和 fd ownership 风险混入 Stage 11。

替代方案：让所有 interrupt/syscall return 都统一抢占。该方案表面一致，但会扩大 ABI 和生命周期风险，不适合当前 maturity。

## Risks / Trade-offs

- [Risk] IRQ-return switch 破坏 `InterruptFrame` 或 generated ISR frame layout -> Mitigation: 实现前审查 `include/irq/interrupt.h`、`kernel/core/irq/interrupt.s`、`kernel/core/sched/switch.s`，增加源码级 frame/order 检查，并记录 validation artifact。
- [Risk] EOI 顺序错误导致丢 IRQ、重复 EOI 或 nested interrupt 异常 -> Mitigation: 外部 IRQ handler 完成后仍只发送一次 EOI；scheduler switch boundary 明确在 EOI 之后或经文档化顺序执行，CPU exception/syscall 不发送 EOI。
- [Risk] 在 scheduler/run queue/wait queue 临界区被抢占导致队列损坏 -> Mitigation: preemption-disable depth 覆盖所有状态转换；pending reschedule 延后到最外层 enable 后处理。
- [Risk] timer IRQ path 过重影响 interrupt latency -> Mitigation: IRQ hook 只做 tick accounting、slice decrement 和 pending 标记；不分配、不释放、不阻塞、不 bulk 输出、不访问 filesystem/user mode。
- [Risk] priority hook 语义过早固化 -> Mitigation: 第一版只声明 bounded hook/reserved field，默认策略保持 round-robin，后续完整 priority policy 另起 change。
- [Risk] runtime smoke 在本地 emulator/toolchain 不可用 -> Mitigation: validation artifact 必须记录缺失工具、替代 source/build checks、跳过原因和残余 IRQ/timer/scheduler 风险。

## Migration Plan

- 先审查并记录当前 `InterruptFrame`、ISR assembly frame、context-switch frame、idle thread ownership、timer IRQ0 hook 和 i8259 EOI ordering。
- 增加 preemption-disable/enable guard、reschedule pending state、time slice accounting 和 scheduler tick hook，保持默认 smoke 关闭。
- 实现 IRQ-return scheduling bridge，并确保 cooperative `yield()`、blocking wait、timeout wakeup 和 idle scheduling 继续通过同一状态机选择 runnable thread。
- 增加 scheduler semantics smoke：覆盖 time slice 到期、preemption-disable 延迟抢占、blocked/sleeping 不被调度、idle 不被无效抢占和 marker 顺序。
- 更新 runtime smoke matrix、docs/en 与 docs/zh 对应文档、validation artifact 和 OpenSpec validation。
- 回滚策略：保留抢占相关 wiring behind default-off smoke/config 或可禁用 guard；若 runtime smoke 暴露低层 ABI 风险，先回退 IRQ-return switch，保留 time slice accounting 和 cooperative scheduler。

## Resolved Decisions

- 第一版 time slice 默认值固定在 scheduler 内部，作为 freestanding kernel policy 常量；scheduler semantics smoke 可通过固定 marker 观察 slice expiry，但不新增 xmake 调试配置来改变默认 slice。
- IRQ-return switch 采用单独的 interrupt-frame-aware bridge，不直接把现有 cooperative switch frame 当作 IRQ return frame 复用；两者可以共享底层 callee-saved context switch helper，但必须显式区分 ISR frame 与普通线程 switch frame。
- Priority hook 第一版只保留 bounded metadata、reserved policy slot 或 source-checkable hook，不实现最小静态 priority 选择逻辑；默认选择策略继续保持单核 round-robin，完整 priority policy 后续独立设计。
