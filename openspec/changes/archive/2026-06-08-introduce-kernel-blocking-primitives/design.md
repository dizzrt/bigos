## Context

BigOS 已完成单核协作式 kernel thread scheduler、PIT IRQ0 monotonic tick、键盘 IRQ1 到 TTY input buffer、`int 0x80` syscall、只读 exFAT 和 default-off 用户态 smoke。当前线程状态和调度策略主要围绕 runnable/running/idle/terminated 展开，TTY 消费者只能轮询空输入，timer 只有 tick 读取和 `mdelay()` busy wait，未来 process `wait`/`exit`、文件描述符和驱动等待都缺少统一阻塞模型。

本 change 位于 roadmap stage 10。它影响 scheduler、timer、TTY、IRQ 上下文契约和 smoke validation，但必须保留现有 x86_64 Legacy BIOS/MBR/exFAT boot path、IDT vector、`InterruptFrame` ABI、i8259 EOI 顺序、syscall vector `0x80`、context-switch callee-saved frame 布局和单核协作式语义。

## Goals / Non-Goals

**Goals:**

- 定义并实现单核协作式等待模型：thread wait states、sleep queue、wakeup、timeout wait 和禁止阻塞上下文。
- 让 scheduler 可跳过 blocked/sleeping 线程，并在 wakeup 后把线程重新放回 runnable 队列。
- 提供 timer-backed sleep/timeout wait，避免把 `mdelay()` 作为普通内核等待的唯一手段。
- 让 TTY 输入成为第一批消费者：IRQ1 producer 仍非阻塞，非中断 consumer 可选择 blocking wait/read。
- 用源码级检查、构建检查和 QEMU headless marker smoke 验证阻塞原语，同时复用阶段 9 runtime smoke matrix 保护现有边界。

**Non-Goals:**

- 不实现 timer-driven preemption、IRQ-return context switch、SMP、IPI、per-CPU run queue 或 TLB shootdown。
- 不引入完整进程生命周期、PID table、父子关系、`wait`/`exit` 的完整语义、`fork`、COW 或 signal。
- 不引入 VFS、fd table、page cache、可写文件系统、异步 IO、userland libc 或 POSIX blocking policy。
- 不改变 boot layout、link address、page-table layout、syscall ABI、IRQ vector、EOI 规则或现有 smoke marker 含义。

## Decisions

### Decision: 采用 intrusive sleep queue 与线程自带 wait 节点

等待队列使用线程控制块内的 wait linkage 或初始化期拥有的 wait node，不在 sleep/wakeup 快路径中动态分配普通内存。线程进入等待前由非中断上下文设置 wait reason、deadline tick 和 queue membership，再通过 cooperative schedule 切走。

选择原因：BigOS 当前 allocator 不能在 IRQ handler 中随意使用，且低层路径需要明确所有权。intrusive 节点避免 wakeup 时分配，也让 timeout 扫描可以只移动已有线程对象。

替代方案：每次 wait 动态分配 queue node。该方案简化 API 表面，但会让 IRQ-safe wakeup、allocation failure 和 reentrancy 规则过早复杂化，因此不采用。

### Decision: 阻塞只发生在显式非中断上下文

新增 `can_block()` 或等价上下文断言，所有 blocking wait API 必须拒绝 IRQ handler、异常 handler、syscall dispatch 内部禁止阻塞区、panic/fatal path、scheduler critical section 和 interrupts-disabled 的不可睡眠区。违反规则时返回确定性错误或触发受控 panic，不能悄悄 busy wait。

选择原因：阶段 10 的核心是先定义等待模型，而不是把每条内核路径都变成可睡眠。显式上下文边界能保护 IRQ、timer、keyboard、page fault 和 syscall ABI。

替代方案：允许任意内核路径阻塞，并由 scheduler 自动处理。该方案需要抢占、锁、可重入 allocator 和更完整的 process/thread policy，超出阶段 10。

### Decision: timeout wait 由 timer tick 做有界到期标记

PIT IRQ0 仍通过 `timer::on_tick()` 推进 monotonic tick。timer 或 scheduler 可在 IRQ context 中执行有界的到期检查/标记或调用 IRQ-safe wakeup hook，但不得分配、释放、阻塞、打印大量日志、访问文件系统或直接执行 IRQ-return context switch。实际调度仍发生在 cooperative yield/schedule 边界。

选择原因：保留现有 timer IRQ contract 和 EOI/iretq 路径，同时让 sleeping thread 能随着 tick 到期被唤醒。阶段 11 再决定是否把 reschedule-on-IRQ-return 升级为完整抢占。

替代方案：在 IRQ0 里直接切换到下一个线程。该方案会触碰 `InterruptFrame` ABI、EOI ordering、critical section 和 stack ownership，属于阶段 11。

### Decision: TTY 保留 poll API 并新增 blocking consumer API

现有 TTY input buffer 仍提供 IRQ-safe enqueue 和非阻塞 poll/drain；新增非中断 blocking read/wait API 只在输入为空时把当前线程挂入 TTY wait queue，并在键盘 IRQ1 enqueue 成功后执行 IRQ-safe wakeup 标记。buffer full/overflow policy 不因 blocking consumer 改变。

选择原因：兼容已有 TTY/keyboard 需求，避免让 IRQ1 producer 依赖 consumer 进度，同时为后续 fd/VFS read 语义留下可复用模型。

替代方案：把所有 TTY read 改为 blocking。该方案会破坏现有 smoke 和非阻塞消费者，且让 boot/debug 路径更难验证。

### Decision: timeout wait 第一版沿用现有负错误码风格

timeout wait 的返回值采用当前 syscall ABI 已使用的确定性负错误码风格：成功或被显式 wakeup 返回非负结果，超时返回一个固定的负值，例如 `-ETIMEDOUT` 等价数值；非法上下文、非法参数等错误也使用同一类小集合负值。阶段 10 不新增全局 kernel errno 枚举，最多在相关 blocking/timer/syscall header 中定义局部 `constexpr` 常量。

选择原因：当前代码已经用 `SYS_ENOSYS = -38`、`SYS_EFAULT = -14` 这类显式负值表达 syscall 错误。沿用该风格可以避免在阻塞原语第一版中过早设计全内核 errno 命名空间，同时保持 smoke 和用户态返回寄存器可验证。

替代方案：立即引入最小 kernel errno 枚举。该方案有利于长期一致性，但会把错误码治理扩大到 allocator、fs、proc、syscall 等更多子系统，超出阶段 10 的等待模型目标。

### Decision: 阶段 10 不允许 syscall dispatch 内部阻塞

阶段 10 保持 `syscall` dispatch 及现有早期 syscall 为 bounded non-blocking 路径；`SYS_DEBUG_WRITE`、`SYS_GET_TICK`、`SYS_WRITE` 和 `SYS_EXIT` 不在 dispatch 内等待 TTY、timer、fd、process lifecycle 或文件系统事件。未来若引入可阻塞 syscall，必须等 process lifecycle、fd table、user buffer copy、sleepable syscall handler 边界和取消/退出语义明确后，在独立设计中逐个开放。

选择原因：`int 0x80` 当前仍由 interrupt frame 进入，并且 header 已声明 dispatch 不应分配或调用非 IRQ-safe allocator。保持 dispatch 本身不可睡眠，可以保护 `InterruptFrame` ABI、CR3/user-copy 边界和早期用户态 smoke。

替代方案：先让个别 syscall 在 dispatch 中直接阻塞。该方案会提前引入当前线程归属、退出竞态、fd 等待对象和用户地址空间访问规则，容易把阶段 10 的 kernel-thread 等待模型扩散成完整 syscall/process 设计。

### Decision: sleeping list 第一版采用有界线性扫描

阶段 10 的 sleeping list 不要求按 deadline 排序，可使用 intrusive unsorted list 或等价简单结构；timer tick 或 scheduler-adjacent 到期处理只做 bounded linear scan，并通过 smoke 规模、最大扫描数量或配置约束保证 IRQ0 路径有界。若后续高线程数或大量 timeout 场景需要扩展，再引入 deadline-ordered list、min-heap 或 timer wheel。

选择原因：当前系统是单核协作式调度，阶段 10 的目标是建立正确的 block/wakeup/timeout 语义，而不是优化大量定时器。简单结构更容易验证 wait queue membership、timeout 结果和 IRQ-safe wakeup 规则。

替代方案：第一版就按 deadline 排序或使用 heap/timer wheel。该方案能改善扩展性，但会增加插入/删除、重复 wakeup、thread teardown unlink 和 IRQ critical section 的复杂度，当前 smoke 规模无法体现收益。

## Risks / Trade-offs

- [Risk] wait queue membership 与 thread lifecycle 交叉导致 use-after-free 或重复入队 -> Mitigation: 线程同一时刻只能属于一个 wait queue；terminated 线程先从 wait queue unlink；源码级检查覆盖 double-enqueue/double-wakeup 约束。
- [Risk] IRQ handler 唤醒路径误用 allocator、console 或 blocking API -> Mitigation: wakeup API 分为 IRQ-safe 标记路径和非中断管理路径；测试扫描 IRQ/timer/keyboard handler 的禁止调用。
- [Risk] timeout 扫描在 IRQ0 中耗时过长 -> Mitigation: 第一版限制 sleeping list 规模和 smoke 用例数量；若需要复杂结构，后续再引入 timer wheel 或 heap。
- [Risk] cooperative 模型下唤醒后无法立即运行 -> Mitigation: 文档和规格明确 wakeup 只让线程 runnable，不保证抢占当前线程；需要调用 yield/schedule 或未来阶段的抢占策略。
- [Risk] blocking TTY smoke 依赖手工输入不稳定 -> Mitigation: 优先用 synthetic/controlled producer 或 deterministic marker；无法自动输入时记录跳过原因和残余风险。

## Migration Plan

- 扩展 thread state 与 scheduler queue 操作，先保持现有 scheduler smoke marker 不变。
- 引入 wait queue、sleep queue、timeout wait 和 context guard，再接入 timer tick 到期唤醒。
- 在 TTY 中保留现有 poll/drain 行为，新增 blocking wait/read API 和对应 smoke。
- 更新 runtime smoke matrix 与 validation artifact，记录 blocking primitives case、工具可用性、日志路径和残余风险。
- 回滚策略：保留新增 API behind default-off smoke/config wiring；若 runtime smoke 失败，可先禁用 blocking smoke，恢复现有 non-blocking TTY、scheduler、timer smoke。
