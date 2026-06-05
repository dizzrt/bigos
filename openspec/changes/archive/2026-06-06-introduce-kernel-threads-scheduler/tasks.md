## 1. Scheduler 基础结构

- [x] 1.1 新增 `include/bigos/sched.h`、`include/bigos/thread.h` 或等价 public header，声明线程状态、线程入口类型、创建/启动/yield API，并标注 non-interrupt-context 与单核限制。
- [x] 1.2 新增 `src/kernel/sched/` 或等价目录，定义 TCB、线程 ID、saved context、栈范围、run queue 节点、idle 线程记录和 scheduler 全局状态。
- [x] 1.3 选择固定容量或 intrusive run queue 表示方式，确保 queue 节点生命周期由 TCB 持有，不依赖 IRQ handler 中的动态分配。
- [x] 1.4 在 `xmake.lua` 中注册新增 C++/assembly 源文件，保持 freestanding C++17、无异常、无 RTTI 构建假设。

## 2. Thread 创建与生命周期

- [x] 2.1 实现 `create_kernel_thread()` 或等价 API，在非中断上下文分配 TCB 与 kernel stack，并初始化线程入口、参数、状态和 saved context。
- [x] 2.2 固定阶段 4 普通内核线程默认使用 1 页 kernel stack，记录 stack base/size，并暂不暴露 smoke/debug 栈页数构建开关。
- [x] 2.3 构造新线程初始栈帧，使首次调度进入 scheduler-owned trampoline，再调用线程入口函数。
- [x] 2.4 实现 `thread_exit()` 或等价终止路径，把线程标记为 terminated 并挂入 terminated list，不立即释放当前线程 TCB/stack。
- [x] 2.5 为创建失败路径提供 deterministic diagnostic 或错误返回，不在 failure path 中破坏 allocator 阶段 3 契约。

## 3. Context Switch

- [x] 3.1 新增 x86_64 context switch assembly helper，保存/恢复 selected calling convention 所需的 callee-saved register 与 stack pointer。
- [x] 3.2 在 C++ scheduler 中封装 context switch 调用边界，明确 old/new context 指针所有权和中断状态假设。
- [x] 3.3 用源码级检查固定 context switch symbol、保存寄存器集合、新线程 trampoline 入口和 `InterruptFrame` ABI 不被 cooperative switch 修改。
- [x] 3.4 对新增 assembly/C++ 文件运行 freestanding syntax 或 narrow build 检查，记录 clang/clangd 辅助诊断与 GCC cross-build 差异。

## 4. 调度策略与 Idle

- [x] 4.1 实现单核 round-robin `yield()`，在至少一个 peer runnable thread 存在时切换到下一个 runnable thread。
- [x] 4.2 实现 `sched::start()` 或等价入口，将 boot/current context 纳入 scheduler 状态，并启动第一个 runnable thread。
- [x] 4.3 实现 scheduler-owned idle thread，用 `hlt` 替代 `kernel()` 尾部裸循环，并文档化 idle 运行时的 IF/IRQ readiness 假设。
- [x] 4.4 修改 `src/kernel/kernel.cc`，保持现有 memory/self-test/TTY/IRQ 初始化顺序不变，在 `irq::enableIRQ()` 后进入 scheduler start path。

## 5. Timer 与 IRQ 集成

- [x] 5.1 在 timer IRQ0 路径接入 bounded scheduler tick accounting/reschedule intent，保持 `timer::on_tick()` tick ownership 不变，且不在阶段 4 实现 IRQ 返回前抢占切换。
- [x] 5.2 确保 timer/keyboard/#PF/IRQ dispatch 路径不调用 ordinary allocator 创建或释放 scheduler 对象。
- [x] 5.3 用源码级检查固定阶段 4 仅提供 cooperative yield + timer intent，确认 IRQ dispatch 仍按既有 EOI、saved frame、register restore 和 `iretq` 语义返回。
- [x] 5.4 保持 CPU exception path diagnostic-only，不把 `#PF` 或其他 fatal exception 接入线程恢复、唤醒或重试逻辑。

## 6. Smoke 与文档

- [x] 6.1 增加默认关闭的 scheduler smoke 构建开关，创建两个 worker thread 输出 bounded deterministic marker 并通过 `yield()` 交替运行。
- [x] 6.2 更新 `docs/arch` 中 scheduler/thread 设计说明，记录单核、无用户态、无 SMP、无阻塞 sleep、allocator context 和 timer/IRQ 边界。
- [x] 6.3 若需要调整 `tools/boot_debug.py` 才能观测 scheduler marker，单独记录为横切工程化项，不把 Python 修改混入本 change，除非明确扩展任务范围。

## 7. Validation

- [x] 7.1 新增或更新 `tests/test_kernel_thread_scheduler_source.py`，覆盖 TCB/context API 标注、1 页默认线程栈、terminated 延后回收、context switch symbol、idle thread 替换裸 `hlt` loop、无 IRQ-return preemption、IRQ allocator 禁止和 smoke marker wiring。
- [x] 7.2 运行 `uv run pytest tests/test_kernel_thread_scheduler_source.py tests/test_memory_interrupt_context_source.py tests/test_timer_irq_foundation_source.py tests/test_interrupt_foundation_source.py` 并记录结果。
- [x] 7.3 运行默认 `xmake`，并在需要时运行 `scheduler_smoke=y` 或等价构建配置，记录 cross-toolchain 构建结果。
- [x] 7.4 对新增/修改 C++ 源和头运行贴近 GCC cross-build 的 freestanding `x86_64-elf-g++ -fsyntax-only` 或 clang/clangd 辅助检查；若工具缺失，记录 blocker 和剩余风险。
- [x] 7.5 在 Bochs/serial oracle 可用时运行 bounded scheduler smoke，观测两个线程 marker；若超时或不可用，记录命令、失败点、历史 oracle 状态和剩余 bootability/scheduler runtime 风险。
- [x] 7.6 运行 `openspec validate introduce-kernel-threads-scheduler --strict`，并在 `validation.md` 中分开记录已通过检查、未运行或未通过检查、历史诊断、当前 change 影响和剩余风险。
