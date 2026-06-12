## Why

阶段 16.5（时间与身份，已完成）已交付 `Process` 的 uid/gid/euid/egid 与纯判定原语 `bigos::cred::may_signal(actor, target)`，但该判定目前没有任何强制点——内核既不能向进程投递任何异步通知，进程也无法相互终止或自愿处理终止请求。路线图阶段 17 要求在 fork/COW（阶段 16）与时间/身份（阶段 16.5）之上补齐**最小 POSIX 信号模型**：这是 shell（阶段 19）做作业控制、可写文件系统（阶段 18）做受控中断、以及未来「一个进程终止另一个进程」语义的硬前置。趁现在语义面仍小（单核、同步、`int 0x80`、已有 IRQ-return reschedule 钩子）时把信号立成最小但正确的地基，可复用现有 `may_signal` 判定与 IRQ-return 边界，避免日后在更复杂的并发模型上补信号。

## What Changes

- 新增最小信号子系统 `bigos::signal`：定义一组固定的信号编号（最小 POSIX 子集，如 `SIGKILL`/`SIGTERM`/`SIGSEGV`/`SIGCHLD`/`SIGUSR1`/`SIGUSR2` 等）、每信号的默认动作（Terminate / Ignore / Core-as-Terminate），以及每进程的 pending 位图（按位、无动态分配，不做实时排队计数）。
- 在 `Process` 中新增信号状态字段（追加、不重排既有布局）：pending 信号位图、阻塞掩码 `sigmask`、每信号处置表（默认/忽略/用户 handler 入口地址）。这些字段在 init/ELF 创建时初始化为默认处置，`fork` 时由子进程逐字段继承（handler 入口与掩码），`exec` 时把用户 handler 重置为默认（pending 与 mask 的 exec 语义在 design 中明确）。
- 在 `int 0x80` ABI 末尾**仅追加**新固定号：`SYS_KILL`（向目标 pid 投递信号，复用 `cred::may_signal` 做「谁能 kill 谁」判定）、`SYS_SIGACTION`（注册/查询某信号的处置）、`SYS_SIGPROCMASK`（查询/修改阻塞掩码）、`SYS_SIGRETURN`（用户 handler 返回内核、恢复被中断的用户上下文）。寄存器 ABI、现有号位、向量布局、DPL 设置、「syscall 不发 EOI」均不变。
- 信号在 **IRQ-return 到用户态的边界**投递：复用现有 `bigos::sched::maybe_preempt_on_irq_return` 同一返回路径上的钩子，只在「被中断帧是用户态（`cs & 0x3 == 3`）、存在未阻塞 pending 信号、当前不在内核临界区」时处理；默认动作（Terminate）走现有 exit/fault-to-reaper 生命周期，用户 handler 动作则在用户栈上构造信号帧并改写返回上下文跳转到 handler，`SYS_SIGRETURN` 负责恢复。`SIGKILL`/`SIGSTOP` 类不可捕获/不可阻塞（本阶段 STOP/CONT 语义见非目标）。
- 架构注意：信号帧构造与用户上下文改写是架构耦合的，本阶段实现集中在 x86_64 的 `InterruptFrame` 与用户栈布局上，但通过一层薄的内核内接口（如 `signal::deliver_pending_to_user(frame, process)`）隔离，使其与「架构抽象」轨道的 trap-frame 接口将来对接，而非把 x86 细节散落到通用信号逻辑里。
- 定义确定性失败语义：向不存在的 pid 投递 -> `-ESRCH`；权限判定拒绝 -> `-EPERM`；非法信号号/非法 handler 地址/越界用户栈 -> `-EINVAL` 或确定性进程 kill（内核态 fault 仍 panic）；信号帧构造失败（用户栈不可写/越界）-> 确定性终止目标进程，绝不在投递热路径分配内存、绝不阻塞。
- 新增默认关闭的验证开关 `signal_smoke`（`BIGOS_SIGNAL_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_SIGNAL_PASSED` / `BIGOS_SIGNAL_FAILED`），覆盖「kill 默认动作终止目标」「用户 handler 捕获 + sigreturn 恢复」「掩码阻塞 pending、解除后投递」「`SIGKILL` 不可捕获」「越权 kill 被 `may_signal` 拒绝」等路径；保留现有 smoke 矩阵不删除。
- **非破坏性**：不改变 `int 0x80` 寄存器 ABI 约定（仅在末尾追加新号）、IDT/向量布局、DPL、页表自映射地址、CR3 切换约定、higher-half/direct-map/`KVMEM_BASE` 布局或用户低半区布局；`#PF` 与外部 IRQ EOI 语义不变；不引入 SMP/锁。

## Capabilities

### New Capabilities
- `signals`: 最小 POSIX 信号能力——固定信号编号集合与默认动作、每进程 pending 位图与阻塞掩码与处置表、`kill`/`sigaction`/`sigprocmask`/`sigreturn` 系统调用、IRQ-return 到用户态边界的信号投递（默认动作走 exit/reaper 生命周期，用户 handler 构造信号帧 + `sigreturn` 恢复）、`SIGKILL` 不可捕获/不可阻塞、基于 `cred::may_signal` 的「谁能 kill 谁」强制点、以及非法输入/越界用户栈的确定性失败语义与默认关闭验证开关。

### Modified Capabilities
- `syscall-entry`: `int 0x80` ABI 在末尾新增 `SYS_KILL`/`SYS_SIGACTION`/`SYS_SIGPROCMASK`/`SYS_SIGRETURN` 号；返回值经 rax 回写，其余 syscall 号、寄存器约定与「syscall 不发 EOI」不变；`SYS_SIGRETURN` 是唯一会在返回时改写被保存用户上下文（恢复信号前帧）的 syscall，需明确其不破坏 `InterruptFrame` 约定。
- `process-lifecycle`: `Process` 新增信号 pending 位图、阻塞掩码与处置表字段；进程创建（init/ELF）初始化为默认处置与空 pending/空掩码；信号默认 Terminate 动作复用现有 exit/fault-to-reaper teardown 与 `exit_code`/`fault_reason` 语义；`SIGCHLD` 在子进程退出时投递给父进程（与现有 wait/reaper 协作，不改变其既有判定）。
- `process-identity-permissions`: `cred::may_signal` 从「仅纯判定、无强制点」升级为「`SYS_KILL` 的实际强制点」；判定语义本身不变（root 放行、否则身份匹配、非法输入拒绝），仅新增其被实际调用的接线点。
- `fork-copy-on-write`: `fork` 复制语义新增「信号处置表与阻塞掩码由父进程继承到子进程、pending 信号集在子进程清空」的要求；不改变既有 COW 地址空间复制、引用计数、回滚与「父返回子 PID、子返回 0」语义。

## Impact

- 受影响子系统：新增 `kernel/core/signal`（信号核心：pending/mask/处置表管理、kill 投递、IRQ-return 投递与信号帧构造、sigreturn 恢复）、`kernel/core/syscall`（新增 4 个 syscall 分发分支）、`kernel/core/irq`（IRQ-return 到用户态边界增加信号投递检查点）、`kernel/core/proc`（`Process` 信号字段、init/ELF 初始化、fork 继承、exec 重置、默认 Terminate 接入 teardown、`SIGCHLD` 投递）、`kernel/core/sched`（与 IRQ-return/exit 生命周期协作）。
- 受影响代码：新增 `include/bigos/signal.h` 与 `kernel/core/signal/*`；[syscall.h](include/bigos/syscall.h) 与 [syscall.cc](kernel/core/syscall/syscall.cc)（新增号位与分支）；[interrupt.cc](kernel/core/irq/interrupt.cc)（IRQ-return 用户态边界投递点）；[proc.h](include/bigos/proc.h) 与 [proc.cc](kernel/core/proc/proc.cc)（信号字段、初始化/继承/重置、默认动作终止、`SIGCHLD`）；[cred.h](include/bigos/cred.h) / [cred.cc](kernel/core/proc/cred.cc)（`may_signal` 接线，判定逻辑不变）；[errno.h](include/bigos/errno.h)（如需 `ESRCH` 等错误码补齐）。
- 构建/验证：`xmake.lua` 新增默认关闭开关 `signal_smoke`；QEMU headless serial-marker smoke 与源码契约/行为断言测试（沿用阶段 14.5 启动的行为断言测试轨道）；clang/clangd 辅助静态检查。
- 假设：x86_64 单核、同步、`int 0x80`、`InterruptFrame` ABI 与向量/DPL 布局不变；信号仅在 IRQ-return 到用户态时投递（不在内核态运行用户 handler，不在内核临界区投递）；用户 handler 在用户栈上构造信号帧并依赖用户态 `SYS_SIGRETURN`（本阶段不提供用户态 libc/trampoline，由 smoke 用户程序自行 `int 0x80` 触发 `SYS_SIGRETURN`）；`kmalloc`/`free` 不参与信号投递热路径（pending/mask/处置表为定长内联字段）；阶段 16 的 fork/COW、阶段 15.5 可增长进程/fd 表、阶段 16.5 身份原语已就位；Bochs/QEMU 经 `tools/boot_debug.py` 验证。
- 非目标：实时信号（`SIGRTMIN`+ 排队计数）、`sigqueue`/`siginfo` 完整负载、`SIGSTOP`/`SIGCONT` 作业停止/恢复语义、`sigsuspend`/`sigpending`/`sigaltstack`/`SA_RESTART` 等完整 `sigaction` 标志、信号驱动的可中断系统调用重启（本阶段同步 syscall 不睡眠故无需 EINTR 重启）、进程组/会话与 `killpg`、core dump 文件、用户态 libc 信号包装与自动 trampoline、SMP 下的跨核信号投递。这些留给阶段 18/19 与后续工作。
