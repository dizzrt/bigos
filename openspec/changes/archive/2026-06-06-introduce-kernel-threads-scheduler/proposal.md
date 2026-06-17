## Why

kernel memory API capability 已把 allocator 与 interrupt-context 的边界固定下来，timer IRQ0、TTY/console 和早期内存路径已经具备进入调度器阶段的最低运行时基础。当前 `kernel()` 仍在初始化后直接进入裸 `hlt` 循环，BigOS 还不能表达多个可运行执行流、idle 线程或面向后续用户态/驱动的调度边界。

kernel thread scheduler capability 需要引入最小内核线程与单核调度器，把系统从“单条初始化路径”推进到“可在多个内核线程之间切换”的早期内核运行时，同时保持现有 boot、IRQ、内存和诊断 ABI 稳定。

## What Changes

- 新增内核线程抽象：线程控制块、内核栈、线程状态、入口 trampoline、就绪队列和基本生命周期。
- 新增 x86_64 kernel context switch 路径，保存/恢复 callee-saved 寄存器、栈指针和必要的返回上下文，不改变既有 `InterruptFrame` ABI。
- 新增单核 round-robin 调度器：先提供显式 `yield()`/cooperative switch，再在 timer tick 路径上定义受控的抢占调度边界。
- 用 idle 线程替代 `kernel()` 末尾的裸 `hlt` loop，使 idle 行为成为 scheduler 管理的线程状态，而不是初始化函数的尾部死循环。
- 将 timer IRQ0 与 scheduler 的交互限定为 bounded、IRQ-context-safe 的调度请求或 tick accounting，不在 IRQ handler 中做普通动态分配、阻塞等待或复杂输出。
- 补充 bounded runtime smoke：创建两个内核线程交替输出确定性 marker，用源码级测试与构建验证锁定上下文切换、初始化顺序和 allocator 使用边界。

非目标：

- 不实现 SMP、per-CPU run queue、IPI、跨 CPU 负载均衡或自旋锁体系。
- 不实现用户态线程、进程模型、ring3 切换、syscall ABI、ELF 用户程序加载或地址空间切换。
- 不实现优先级调度、CFS、实时调度、wait queue、sleep queue、阻塞 IO 或 scheduler sleep。
- 不改变 boot 固定地址、linker higher-half base、kernel load base、BootInfo handoff ABI、direct map、`KVMEM_BASE` heap/vmalloc 语义、IDT vector 分配或 `InterruptFrame` layout。

## Capabilities

### New Capabilities

- `kernel-thread-scheduler`: 定义 BigOS 单核早期内核线程、context switch、round-robin 调度、idle 线程、timer/yield 调度边界和验证要求。

### Modified Capabilities

- `timer-irq-runtime`: 在保持 `timer::on_tick()` IRQ-context-safe 契约的前提下，补充 timer tick 与 scheduler 的受控交互要求，例如只设置 reschedule intent 或执行 bounded tick accounting，不引入 IRQ handler 动态分配、阻塞或复杂输出。
- `interrupt-exception-foundation`: 在不改变 IDT、exception/IRQ 分流、EOI 规则和 `InterruptFrame` ABI 的前提下，补充从 IRQ 返回路径触发调度决策时必须保持寄存器/栈恢复语义稳定的要求。

## Impact

- 受影响子系统：`kernel/core/kernel.cc` 初始化尾部、`kernel/core/timer`、`kernel/core/irq`、新增 `kernel/core/sched` 或等价调度器目录、`include/bigos` public scheduler/thread headers、`xmake.lua` 源文件注册、`tests` 源码级验证与 `docs/en/arch` 架构说明。
- 架构假设：x86_64、单核、Legacy BIOS/i8259、PIT IRQ0、kernel-owned IDT、无 SMP、无用户态地址空间、无进程模型。
- 内存假设：线程栈和 TCB 可在非中断上下文创建；调度器和 IRQ handler 遵守kernel memory API capability 的 allocator 契约，普通 allocator 仍不从 IRQ handler 调用。
- 工具链/模拟器假设：继续使用 `xmake`、`x86_64-elf-g++`、`uv run pytest` 和 `openspec validate`；Bochs runtime smoke 可用时观测 serial/VGA marker，不可稳定观测时必须在 validation 中记录剩余 bootability/scheduler runtime 风险。
