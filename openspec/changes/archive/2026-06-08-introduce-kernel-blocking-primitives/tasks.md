## 1. 调度状态与上下文边界

- [x] 1.1 审查 `kernel/core/sched`、`include/bigos/thread.h`、`include/bigos/sched.h` 的线程状态、run queue、idle thread 和 context-switch frame 现状，记录不能改变的 ABI 与所有权边界。
- [x] 1.2 扩展线程状态模型，加入 blocked/sleeping 或等价非 runnable 状态，并确保 scheduler 只选择 runnable 线程。
- [x] 1.3 增加 scheduler/context guard，用于判断当前是否允许阻塞，并覆盖 IRQ、异常、fatal path、scheduler critical section、interrupts-disabled 等禁止阻塞上下文。
- [x] 1.4 保持阶段 10 单核协作式语义，不增加 IRQ-return context switch、time slice 抢占、SMP、per-CPU run queue 或 IPI。

## 2. Wait Queue 与 Wakeup

- [x] 2.1 设计并实现 freestanding-safe 的 wait queue 数据结构，优先使用线程内 intrusive linkage 或初始化期拥有节点，避免 sleep/wakeup 快路径动态分配。
- [x] 2.2 实现当前线程进入 wait queue 的阻塞流程，保证入队、状态切换、run queue 移除和 schedule 切换顺序可审查。
- [x] 2.3 实现 wake-one/wake-all 或最小 wakeup API，保证等待线程只被移回 runnable 队列一次，空队列 wakeup 安全返回。
- [x] 2.4 处理 wait queue 与 thread exit/termination 的交叉边界，避免 terminated thread 遗留在 wait queue 或重复 unlink。

## 3. Timer Sleep 与 Timeout

- [x] 3.1 在 timer/scheduler 边界实现基于 monotonic tick 的 sleep/timeout wait API，记录 deadline 与 timeout 返回结果。
- [x] 3.2 将 expired sleepers 的处理接入 timer tick 或受控 scheduler hook，保持 `timer::on_tick()` IRQ-context-safe 且不直接暴露 timer-internal tick storage。
- [x] 3.3 确保 IRQ0 路径只执行有界标记或 IRQ-safe wakeup，不分配、不释放、不阻塞、不 bulk 输出、不访问文件系统、不切换线程。
- [x] 3.4 为 timeout 被显式 wakeup、timeout 到期、timeout tracking removal 和重复 wakeup 编写源码级检查或最小单元/静态测试。

## 4. TTY Blocking Consumer

- [x] 4.1 保留现有 TTY 非阻塞 poll/drain API 和 IRQ-safe enqueue 语义，确认键盘 IRQ1 producer 不依赖 consumer 进度。
- [x] 4.2 增加非中断上下文 blocking TTY input wait/read API，在 buffer 为空时把当前线程挂入 TTY wait queue。
- [x] 4.3 在 keyboard/TTY enqueue 成功后通过 bounded IRQ-safe wakeup 标记或唤醒等待 reader，禁止 IRQ1 handler 分配、睡眠、`mdelay()`、文件系统访问或直接 context switch。
- [x] 4.4 增加 deterministic TTY blocking smoke producer 或 timeout 路径；若必须依赖手工键盘输入，记录自动化缺口和残余风险。

## 5. Smoke、构建与工具链集成

- [x] 5.1 增加 default-off blocking primitives smoke 开关和固定 `BIGOS_` serial markers，覆盖 block、wake、timeout、resume/completion 状态。
- [x] 5.2 将 blocking primitives case 加入 runtime smoke matrix，列出 xmake switches、expected markers、timeout、serial log path、QEMU headless flow 和跳过条件。
- [x] 5.3 保持已有 memory、timer、scheduler、syscall、filesystem、user program、user ELF smoke switches 默认关闭且 marker 含义不变。
- [x] 5.4 如果实现修改 Python helper 或测试脚本，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，记录 blocker。

## 6. 文档与验证

- [x] 6.1 更新相关英文 canonical 文档和简体中文镜像，说明 blocking context、wait queue、timeout wait、TTY blocking consumer、非目标和阶段 10 边界。
- [x] 6.2 运行 `openspec validate introduce-kernel-blocking-primitives --strict` 并修复本 change 引入的 OpenSpec 格式或需求问题。
- [x] 6.3 对 C++/header 改动运行最窄有用 `xmake` / `x86_64-elf-gcc` cross build；若工具链不可用，记录缺失工具、替代检查和残余 bootability 风险。
- [x] 6.4 对修改过的 C++ 源码和头文件执行 clang/clangd 辅助诊断，尽量使用 freestanding C++17、x86_64 target、no exceptions、no RTTI 和项目 include path；区分历史诊断、当前变更诊断和 freestanding false positive。
- [x] 6.5 使用 QEMU headless serial-marker smoke 验证 blocking primitives case；涉及 IRQ/timer/port-IO 行为时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证。
- [x] 6.6 生成或更新 validation artifact，明确记录通过检查、跳过/blocked 检查、日志路径、工具可用性、残余 scheduler/timer/IRQ 风险和未执行原因。
