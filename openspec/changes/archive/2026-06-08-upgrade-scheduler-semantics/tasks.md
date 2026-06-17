## 1. ABI 与当前状态审查

- [x] 1.1 审查 `include/irq/interrupt.h`、`kernel/core/irq/interrupt.s`、`kernel/core/irq/interrupt.cc`、`kernel/core/sched/switch.s` 和 `kernel/core/sched/sched.cc`，记录 `InterruptFrame`、ISR stack layout、context-switch frame、EOI 顺序和 idle ownership 的不可破坏边界。
- [x] 1.2 审查 timer IRQ0、scheduler tick hook、blocked/sleeping wakeup、TTY/keyboard wakeup 和 syscall dispatch 的当前上下文规则，列出哪些路径可抢占、不可抢占、可阻塞或禁止阻塞。
- [x] 1.3 确认 bounded timer-driven scheduler semantics 不改变 boot layout、link address、IDT vector、syscall vector `0x80`、page-table layout、disk image layout、ATA PIO/exFAT 读路径或现有 smoke marker 含义。

## 2. Preemption Guard 与调度状态

- [x] 2.1 为 scheduler 增加 preemption-disable/enable guard 或等价 bounded state，覆盖 run queue、wait queue、sleep list、thread state transition 和 context-switch preparation。
- [x] 2.2 增加 pending reschedule intent 状态，确保 timer IRQ 可记录 intent，但在 preemption-disabled 或不可抢占上下文中不会执行 IRQ-return switch。
- [x] 2.3 将 blocking-context guard 与 preemption guard 规则对齐，确保 scheduler critical section 中不能 sleep，也不能被 timer-driven preemption 切走。
- [x] 2.4 增加源码级检查或断言，覆盖 preemption-disable depth 配对、pending intent 不丢失、不可抢占上下文拒绝 IRQ-return switch。

## 3. Time Slice 与 Scheduler Policy

- [x] 3.1 为 ordinary runnable thread 增加 bounded time slice accounting，并在调度到线程时刷新或分配 slice。
- [x] 3.2 在 timer IRQ scheduler hook 中递减或记录当前线程 slice，保持 hook IRQ-safe、无分配、无释放、无阻塞、无 bulk 输出、无 filesystem/user-mode 依赖。
- [x] 3.3 保持 cooperative `yield()`、blocking wait、timeout wakeup、blocked/sleeping skip 和 idle scheduling 行为兼容现有状态机。
- [x] 3.4 增加 priority hook、静态 priority 字段或 reserved policy slot，并明确默认策略仍是单核 round-robin，完整 priority scheduler 延后。

## 4. IRQ-Return Scheduling Bridge

- [x] 4.1 设计并实现外部 IRQ return 边界的 scheduler bridge，保证 handler 完成、单次 EOI、frame layout 和 `iretq` return 语义可审查。
- [x] 4.2 确保 bridge 只在 preemption enabled、当前线程 eligible、当前线程仍 runnable、非 fatal/exception/syscall forbidden region 时切换。
- [x] 4.3 确保 CPU exception handler、`#PF` fatal path 和 `int 0x80` syscall ABI 不成为调度恢复、sleep 或 process-lifecycle 路径。
- [x] 4.4 增加源码级 frame/order 检查或测试，覆盖 saved register order、error-code slot、EOI ordering、IRQ-return switch guard 和 allocator 禁用规则。

## 5. Smoke 与 Runtime Matrix

- [x] 5.1 增加 default-off scheduler semantics smoke 开关和固定 `BIGOS_` serial markers，覆盖 time slice expiry、timer-driven reschedule、preemption-disable 延迟切换和 pending intent 处理。
- [x] 5.2 扩展 runtime smoke matrix，列出 scheduler semantics case 的 xmake switches、expected markers、timeout、serial log path、QEMU headless flow、skip/block 条件和 Bochs/QEMU+Bochs 交叉验证建议。
- [x] 5.3 保持已有 memory、timer、cooperative scheduler、blocking、syscall、filesystem、user program、user ELF smoke switches 默认关闭且 marker 含义不变。
- [x] 5.4 若修改 Python helper 或测试脚本，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，记录 blocker、替代检查和残余风险。

## 6. 文档与验证

- [x] 6.1 更新相关英文 canonical 文档和简体中文镜像，说明 bounded timer-driven scheduler semantics scheduler semantics、time slice、preemption-disable、IRQ-return switch、priority hook、非目标和验证方式。
- [x] 6.2 运行 `openspec validate upgrade-scheduler-semantics --strict` 并修复本 change 引入的 OpenSpec 格式或需求问题。
- [x] 6.3 对 C++/header/assembly 改动运行最窄有用 `xmake` / `x86_64-elf-gcc` cross build；若工具链不可用，记录缺失工具、替代检查和残余 bootability 风险。
- [x] 6.4 对修改过的 C++ 源码和头文件执行 clang/clangd 辅助诊断，尽量使用 freestanding C++17、x86_64 target、项目 include path、no exceptions、no RTTI；区分历史诊断、当前变更诊断和 freestanding false positive。
- [x] 6.5 使用 QEMU headless serial-marker smoke 验证 scheduler semantics case；涉及 IRQ/timer/port-IO/context-switch 行为时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证。
- [x] 6.6 生成或更新 validation artifact，明确记录通过检查、跳过/blocked 检查、日志路径、工具可用性、残余 scheduler/timer/IRQ 风险和未执行原因。
