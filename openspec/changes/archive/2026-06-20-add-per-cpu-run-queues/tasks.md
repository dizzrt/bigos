## 1. Scheduler State And Locking

- [x] 1.1 将现有 BSP-only scheduler state 拆分为 bounded per-CPU scheduler domain，覆盖 current、idle、run queue、sleep list、terminated list、reschedule state、preemption/critical counters 和统计字段。
- [x] 1.2 增加 scheduler domain 初始化路径，使 BSP-only 配置保持现有单 CPU 行为，online AP 在参与调度前拥有完整 scheduler domain。
- [x] 1.3 定义 run queue / wait queue / sleep list 的 IRQ-safe SMP 锁边界，并实现跨 CPU 操作的 CPU id 升序锁顺序。
- [x] 1.4 审查并移除 AP tick、IRQ-return preemption、wake path 中对 BSP-only scheduler singleton 的直接依赖。

## 2. Per-CPU Scheduling Paths

- [x] 2.1 将 `create_kernel_thread` 扩展为显式选择 online CPU 的 bounded placement，并保证 offline/failed CPU 不接收 runnable work。
- [x] 2.2 将 `yield`、idle loop、thread exit 和 context switch 前后处理改为使用当前 CPU scheduler domain，并保持既有 context-switch ABI 不变。
- [x] 2.3 将 sleep timeout 与 wait queue wakeup 改为通过 CPU-owned scheduler domain 重新入队，确保每个 thread 最多位于一个 run queue。
- [x] 2.4 将 timer tick time-slice accounting 与 IRQ-return preemption 改为 CPU-local 逻辑，使 AP tick 能驱动 AP 本地调度。
- [x] 2.5 更新 proc/scheduler context restore 边界，确保 current process、address-space root、TSS/RSP0 与 current thread 的 CPU-local ownership 一致。

## 3. Cross-CPU Wakeup And Scheduler Nudge

- [x] 3.1 实现 remote enqueue helper，按先发布 runnable state、再设置目标 CPU reschedule pending 的顺序完成跨 CPU wakeup。
- [x] 3.2 增加 scheduler-owned nudge 边界，必要时通过 LAPIC IPI 或等价机制唤醒目标 CPU 重新评估调度。
- [x] 3.3 为 scheduler nudge 分配并接入受控 interrupt/dispatch 路径，保持 syscall vector、exception frame、legacy i8259 EOI 和 LAPIC EOI 语义不混淆。
- [x] 3.4 明确 scheduler nudge 不承诺 generic IPI、TLB shootdown、CPU hotplug 或完整 APIC default interrupt delivery，并在源码注释和规格相关文档中保持该边界。

## 4. Validation And Documentation

- [x] 4.1 增加或更新源码级检查，覆盖 CPU-local scheduler state、remote enqueue 发布顺序、锁顺序、IRQ context allocation exclusion、AP tick 不访问 BSP-only 调度状态。
- [x] 4.2 增加 bounded multi-core scheduler smoke，验证 runnable work 可在多个 online CPU 上执行，并确认普通 bounded userland baseline 未回归。
- [x] 4.3 运行 `openspec validate add-per-cpu-run-queues --strict` 并修复当前 change 引入的规格问题。
- [x] 4.4 运行最窄可用 `xmake` 交叉编译；若 `x86_64-elf-gcc`、`xmake` 或本地配置缺失，记录阻塞原因和残余风险。
- [x] 4.5 运行 C++ 辅助静态检查或 clang/clangd 等价诊断；若 freestanding x86_64 配置不可用，记录配置差距、历史诊断和当前 change 风险。
- [x] 4.6 使用 QEMU 多核 headless smoke 验证 scheduler 行为；可用时补充 Bochs 交叉验证，若 Bochs 多核或显示/ROM 配置不可用，明确记录跳过项。
- [x] 4.7 更新相关架构文档，保持 `docs/en` 与 `docs/zh` 镜像同步，并避免在文档中声称完整 SMP、完整 IPI/TLB shootdown 或完整 APIC interrupt migration。
