## Context

BigOS 当前初始化顺序为 `init_mem()`、可选 `mm::self_test()`、`terminal::init_tty()`、`irq::initIRQ()`、`sti`，随后在 `kernel()` 末尾输出 boot marker 并进入裸 `hlt` loop。unified boot handoff capability/1.5 已提供 PIT IRQ0 和 timer-owned `on_tick()`，TTY console input capability 已接入 TTY/console 输入输出边界，kernel memory API capability 已明确普通 allocator 不可从 IRQ handler 调用，并用 `InterruptGuard` 保护单核 allocator metadata 更新。

kernel thread scheduler capability 的调度器必须在这些约束内推进：只面向 x86_64 单核早期内核；不改变 boot/linker/memory 地址布局；不改变 IDT vector、`InterruptFrame` layout、EOI 分离规则或 `#PF` diagnostic-only 语义；不声明 SMP、用户态或阻塞内核服务能力。

## Goals / Non-Goals

**Goals:**

- 定义最小内核线程模型：TCB、线程 ID、状态、入口函数、内核栈、run queue 和 idle 线程。
- 提供 freestanding-safe 的 x86_64 context switch，在线程之间保存/恢复必要寄存器和栈。
- 建立单核 round-robin 调度路径：kernel thread scheduler capability 首个可运行版本只通过显式 `yield()` 切换，timer tick 仅记录受控 reschedule intent。
- 将 `kernel()` 尾部裸 `hlt` loop 收敛为 scheduler-owned idle 线程。
- 提供 bounded smoke 与源码级检查，证明两个内核线程可交替运行且调度路径遵守kernel memory API capability allocator/IRQ 契约。

**Non-Goals:**

- 不实现 SMP、per-CPU run queue、IPI、自旋锁体系或跨 CPU 同步。
- 不实现进程、用户态地址空间、ring3 切换、syscall、fork/exec 或用户程序加载。
- 不实现优先级、CFS、实时调度、sleep queue、wait queue、阻塞 IO 或 scheduler sleep。
- 不改变 boot 固定地址、higher-half base、kernel load base、BootInfo ABI、direct map、`KVMEM_BASE`、IDT vector 分配或 `InterruptFrame` ABI。

## Decisions

### Decision: 先建立单核 TCB + intrusive run queue

线程对象由调度器拥有，包含线程 ID、状态、栈范围、saved stack pointer、入口函数和 intrusive queue 节点。run queue 使用固定的 scheduler-owned 结构或 KTL intrusive list，避免调度器队列节点生命周期与普通 heap 容器耦合。

理由：kernel thread scheduler capability 只需要单核早期线程，不需要泛型容器复杂性。intrusive queue 明确对象生命周期，便于源码级验证调度路径不在 IRQ handler 中动态分配。

替代方案：使用 allocator-backed map/list 管理线程。该方案会过早扩大 allocator 与 scheduler 的耦合，并增加 IRQ/preemption 边界风险。

### Decision: 线程栈和 TCB 只在非中断上下文创建

`create_kernel_thread()` 或等价 API 只能在非中断上下文调用，允许使用 `alloc_kernel_pages()`/`kmalloc()` 取得 TCB 与栈；IRQ handler、timer `on_tick()` 和 `irq_dispatch()` 不创建线程、不释放线程、不做普通动态分配。

理由：这直接继承kernel memory API capability 的 allocator 契约，避免把普通 allocator 误升级为 IRQ-safe。

替代方案：为 IRQ handler 引入 emergency stack/TCB pool。该方案更接近驱动或实时场景，但不是kernel thread scheduler capability 所需的最小调度器。

### Decision: context switch 使用独立 x86_64 assembly helper

新增 `switch_context(old_sp*, new_sp)` 或等价汇编入口，保存 callee-saved 寄存器与当前栈指针，加载目标线程栈并返回到目标上下文。新线程栈由 trampoline 预构造，使第一次调度时进入线程入口函数；线程入口返回时进入 `thread_exit()` 或 panic/halt 路径。

理由：显式汇编更符合当前低层风格，也避免依赖 hosted runtime、异常或编译器协程能力。

替代方案：用 `setjmp`/`longjmp` 或 C++ coroutine。两者都不适合当前 freestanding runtime，也会隐藏 ABI 细节。

### Decision: kernel thread scheduler capability 固定为 cooperative yield，timer 只记录 reschedule intent

kernel thread scheduler capability 不实现 IRQ 返回前抢占切换。调度切换只由 `sched::yield()` 在非中断上下文显式触发。timer IRQ0 继续调用 `timer::on_tick()`，并只执行 bounded scheduler tick accounting 或设置 reschedule intent；该 intent 只供后续非中断上下文检查或未来 change 扩展，不在当前阶段改变 IRQ 返回路径。

理由：cooperative path 能先验证 TCB、栈和 context switch；IRQ 返回前抢占涉及 IRQ frame、EOI、返回路径和 allocator 约束，应留给后续专门 change 设计和验证。

替代方案：直接在 IRQ0 handler 内切换线程。该方案风险较高，容易破坏当前 ISR ABI、EOI 顺序和kernel memory API capability 的 IRQ handler 限制。

### Decision: kernel thread scheduler capability 固定 1 页内核线程栈

kernel thread scheduler capability 的普通内核线程栈默认固定为 1 个 kernel page，不新增 smoke/debug 构建开关调整栈大小。TCB 必须记录栈 base/size，源码级检查应固定默认页数和栈范围记录。若后续需要更大栈或 guard page，应在单独 change 中结合栈溢出诊断一起推进。

理由：1 页栈足以覆盖 bounded smoke、yield 和最小 trampoline，同时保持内存占用、失败模式和源码级验证简单。构建开关过早暴露会扩大测试矩阵，并掩盖栈使用过大的问题。

替代方案：提供 `scheduler_stack_pages` 构建开关。该方案更灵活，但会让kernel thread scheduler capability 的 smoke 和 validation 产生多配置语义，不利于先稳定最小调度器。

### Decision: 线程退出进入 terminated 列表并延后回收

kernel thread scheduler capability 的 `thread_exit()` 不立即释放当前线程的 TCB 或 kernel stack，而是把线程标记为 terminated，并挂入 scheduler-owned terminated list。实际回收推迟到后续 safe reclamation 设计；当前阶段只保证 terminated 线程不会再次进入 runnable queue。

理由：当前线程正在使用自己的栈执行退出路径，立即释放栈/TCB 容易和 context switch 交错。延后回收降低首版 context switch 风险，也避免在调度关键路径引入复杂生命周期协议。

替代方案：切换到 reaper 线程后立即回收。该方案需要额外线程角色和更明确的 reclaim 时机，超出kernel thread scheduler capability 最小调度器范围。

### Decision: idle 线程拥有 halt 行为

`kernel()` 完成初始化后创建或注册 boot/main 线程、smoke worker 线程和 idle 线程，最后进入 `sched::start()`。idle 线程在无其他 runnable 线程时执行 `hlt`，并依赖 IRQ 唤醒后重新进入调度决策。

理由：idle 线程让 CPU halt 成为 scheduler 策略，而不是 `kernel()` 尾部不可调度的死循环，为后续 sleep/wait queue 和用户态进程奠定结构。

替代方案：保留 `kernel()` 裸 `hlt` loop 并从 timer IRQ 中临时调度。该方案会把初始化函数与运行时调度耦合，难以扩展。

## Risks / Trade-offs

- [Risk] context switch 栈布局错误导致 `ret`/寄存器恢复崩溃 -> 通过 freestanding syntax check、源码级栈布局断言和 bounded Bochs smoke 交叉验证。
- [Risk] timer preemption 破坏 EOI 或 `iretq` 返回语义 -> kernel thread scheduler capability 明确不做 IRQ 返回前抢占切换，timer 路径只做 bounded intent；抢占切换留给后续 change。
- [Risk] 调度器路径误从 IRQ handler 调用 ordinary allocator -> 源码级测试扫描 timer/keyboard/#PF/scheduler tick 路径，要求线程创建/销毁仅在非中断上下文。
- [Risk] 1 页线程栈不足以承载未来复杂内核路径 -> kernel thread scheduler capability 只运行 bounded smoke 和最小 trampoline；后续扩大栈或 guard page 需单独设计。
- [Risk] terminated 线程延后回收造成内存不回收 -> kernel thread scheduler capability 线程数量 bounded，validation 记录该限制；安全回收留给后续 lifecycle change。
- [Risk] idle 线程 `hlt` 与 IF 状态不匹配导致无法被 IRQ 唤醒 -> idle loop 文档化必须在 IRQ enabled 状态运行，并用 source checks 覆盖 `sched::start()` 前的 IRQ readiness。
- [Risk] Bochs serial marker 当前不稳定，runtime smoke 可能无法作为 oracle -> validation 必须记录源码/构建检查通过项、实际 Bochs 命令和剩余 bootability 风险。

## Migration Plan

1. 新增 scheduler/thread headers、C++ 实现和 x86_64 context switch assembly，并接入 `xmake.lua`。
2. 在 `kernel()` 保持当前初始化顺序不变，`irq::enableIRQ()` 后创建 smoke worker/idle 线程并调用 `sched::start()`。
3. 以 cooperative `yield()` 验证两个线程交替 marker，并把 timer tick 的 reschedule intent 接入为 bounded accounting/intent 记录，不实现 IRQ 返回前抢占。
4. 更新架构文档、OpenSpec specs、源码级测试和 validation 记录。
5. 若实现失败，可回退 `kernel()` 尾部为原 `hlt` loop，并保持新增调度器代码不接入启动路径。
