## Why

BigOS 当前已经具备单核协作式调度、PIT tick、TTY 输入、syscall 和 smoke-only 用户态闭环，但内核仍缺少统一等待模型；继续扩展进程生命周期、文件描述符、文件系统或设备驱动前，需要先明确线程何时可以阻塞、如何被唤醒、超时如何表达，以及哪些上下文绝对不能睡眠。

blocking primitives and timer ownership capability 聚焦建立可验证的阻塞与睡眠原语，保持单核协作式语义，不把完整抢占、SMP、通用进程生命周期或 VFS 一起引入。

## What Changes

- 增加内核等待模型：线程可进入 bounded wait/sleep 状态，并通过 sleep queue 或显式 wait object 从非中断上下文阻塞。
- 增加 wakeup 语义：非中断路径和受限 IRQ-safe 路径可以唤醒等待线程，但 IRQ handler 不分配、不释放、不阻塞。
- 增加 timeout waits：基于现有 PIT monotonic tick 支持有界超时等待，作为 `mdelay()` 之外的可让出 CPU 的等待方式。
- 扩展协作式调度器状态机：在 runnable/running/idle/terminated 之外识别 blocked/sleeping 类状态，并只调度 runnable 线程。
- 为第一批消费者定义边界：TTY 输入可提供非中断上下文的 blocking read/wait，timer 提供 sleep/timeout wait，未来 process `wait`/`exit` 只保留接口预留和非目标说明。
- 增加验证：在runtime smoke validation matrix runtime smoke matrix 基础上加入 blocking primitives 的源码级检查、构建检查和 QEMU headless marker smoke；低层 IRQ/timer 行为仍建议 Bochs 或 QEMU+Bochs 交叉验证。

## Capabilities

### New Capabilities

- `kernel-blocking-primitives`: 定义单核协作式内核等待模型，包括线程等待状态、sleep queue、wakeup、timeout wait、禁止阻塞上下文和blocking primitives and timer ownership capability 验证要求。

### Modified Capabilities

- `kernel-thread-scheduler`: 调度器需求从仅 runnable/running/idle/terminated 扩展到可跳过 blocked/sleeping 线程，并保留非抢占、单核、不在 IRQ return 切换的边界。
- `timer-irq-runtime`: timer 需求增加 timeout/sleep wait 与 wake expired sleepers 的协作式集成，同时保持 `on_tick()` IRQ-context-safe 和不分配/不阻塞。
- `tty-console-input`: TTY 输入需求增加非中断上下文 blocking wait/read 消费者，同时保持键盘 IRQ1 producer 固定容量、非阻塞、无分配。
- `runtime-smoke-validation`: runtime smoke matrix 增加 blocking primitives case，并记录 QEMU headless marker、跳过原因、低层交叉验证建议和残余风险。

## Impact

- 受影响子系统：`kernel/core/sched`、`include/bigos/sched.h`、`include/bigos/thread.h`、`kernel/core/timer`、`include/bigos/timer.h`、`kernel/core/terminal`、`include/bigos/tty.h`、`kernel/core/irq` 中与上下文规则相关的调用边界，以及 smoke/validation helper。
- 架构假设：仅 x86_64 单核 Legacy BIOS 路径；不修改 IDT vector、`InterruptFrame` ABI、syscall vector `0x80`、i8259 EOI 语义或 context-switch callee-saved frame 布局。
- 内存假设：等待队列节点和线程状态存储由初始化期或非中断上下文分配；IRQ handler 不通过普通 allocator 创建/销毁 wait object、TCB 或 queue node。
- emulator 与工具链假设：继续使用 `xmake`、`x86_64-elf-*` 工具链和 QEMU headless marker smoke；涉及 IRQ/timer/port-IO 语义时在可用环境下保留 Bochs 或 QEMU+Bochs 交叉验证。
- 磁盘/文件系统假设：不改变 Legacy BIOS/MBR/exFAT raw image 布局，不引入可写文件系统、VFS、page cache、异步 IO、UEFI/OVMF、virtio/AHCI/NVMe。
- 非目标：不实现完整抢占式调度、SMP、PID/进程表、`wait`/`exit` 的完整进程语义、`fork`/COW、signal、通用 userland libc 或 POSIX blocking IO。
