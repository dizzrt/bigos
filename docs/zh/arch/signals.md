# 最小信号模型

本阶段在既有进程模型（fork/COW）、身份/权限原语、`int 0x80` syscall 入口与受
保护的 IRQ-return 重调度钩子之上，新增一个最小但正确的 POSIX 信号模型。它刻意
保持小：固定的非实时信号编号、每进程内联信号状态、单一 IRQ-return 投递点，以及
面向默认终端 interrupt-like input 的有界 process-group 投递。不引入实时信号、完整
作业控制、`termios`、SMP 或完整 POSIX libc。

## 信号编号集合与默认动作

`include/bigos/signal.h` 定义一组固定的非实时信号编号，复用 POSIX/Linux 惯用值：
`SIGINT = 2`、`SIGKILL = 9`、`SIGUSR1 = 10`、`SIGSEGV = 11`、`SIGUSR2 = 12`、
`SIGTERM = 15`、`SIGCHLD = 17`。合法编号为 `1..SIG_MAX`（`SIG_MAX = 31`），因此每个信号映射到每
进程 `uint64_t` 位图（`SigSet`）中的单个位 `1ull << (signo - 1)`。位图宽度与最高
信号号一致且 `<= 64`。

每个信号有确定性默认动作：

- Terminate：`SIGINT`、`SIGKILL`、`SIGTERM`、`SIGSEGV`、`SIGUSR1`、`SIGUSR2` 及其余无特殊
  默认的编号的默认动作。
- Ignore：`SIGCHLD` 的默认动作。

同一信号在被取走前重复投递合并为单个 pending 位（标准非实时语义，无实时排队/计
数）。`SIGKILL` 不可捕获、不可阻塞：无论处置或阻塞掩码如何，恒终止。

## 每进程信号状态

`Process` 新增三个追加的、定长内联字段（既有布局不变）：

- `sig_pending`（`SigSet`）：pending 信号位图。
- `sig_mask`（`SigSet`）：阻塞掩码。被阻塞信号保持 pending 直到解除阻塞；
  `SIGKILL` 永远无法加入掩码。
- `sig_disp[SIG_COUNT]`（`SigDisposition`）：按 `signo - 1` 索引的每信号处置表，
  每项为 `Default`、`Ignore` 或带用户态 handler 入口地址的 `Handler`。

所有信号投递与查询路径都操作这些内联字段，绝不分配或阻塞。

生命周期：

- init（PID 1）与非 fork ELF 创建：`signal::init_state` 将全部处置置为默认、空掩
  码、空 pending。
- `fork_current`：`signal::inherit_on_fork` 把处置表与阻塞掩码逐字段拷贝到子进
  程，并清空子进程 pending（POSIX fork）。不新增分配、不新增失败路径，不改变
  COW/引用计数/回滚以及「父返回子 PID、子返回 0」语义。
- `exec`：`signal::reset_handlers_on_exec` 把所有 `Handler` 处置重置为默认（旧
  handler 入口已是失效用户地址），同时保留阻塞掩码与 pending（决策 10）。

## kill 投递与权限强制

`signal::kill(target, signo)` 对合法信号在目标置位 pending，越界编号返回
`-EINVAL`。它不做权限判定。`SYS_KILL` syscall 是唯一强制点：查找目标 PID（不存在
-> `-ESRCH`），强制 `cred::may_signal(actor, target)`（拒绝 -> `-EPERM`，不改目标
pending），随后才调用 `signal::kill`（非法信号 -> `-EINVAL`，成功 -> 0）。
`cred::may_signal` 判定逻辑不变（root 放行、否则身份匹配、空输入拒绝）。

默认终端在非 IRQ 上下文消费 interrupt-like input 时，也可以把有界 `SIGINT` 投递给
当前数值型 foreground process group。Keyboard IRQ 只入队输入并唤醒 waiter；
process-group 遍历、权限检查与 pending bit 更新均在普通用户进程 syscall 上下文完成。

## IRQ-return 到用户态边界的投递

信号在唯一一点投递（决策 2 与 9）：`irq_dispatch` 的外部 IRQ 分支中，在
`sched::maybe_preempt_on_irq_return` 之后、`iretq` 之前，且仅当被中断帧为用户态
（`(cs & 0x3) == 0x3`）、当前进程存在、且存在可投递信号（未阻塞，或像 `SIGKILL`
那样不可阻塞）时。内核态被中断帧绝不投递用户信号，且该路径不发自身的 i8259 EOI。

`signal::deliver_pending_to_user` 选取编号最小的可投递信号，清除其 pending 位，
并按处置动作处理：

- `SIGKILL` 或默认 Terminate 处置走既有 `fault_current_and_exit` 生命周期，把信号
  编码为 `-(128 + signo)` 进退出/fault 状态，供父进程 `wait` 或诊断观察。既有
  zombie/reaper、`wait_status_consumed`、`parent_waiting` 语义不变。
- Ignore 处置（如无 handler 的 `SIGCHLD`）清位并原样返回用户态。
- 用户 `Handler` 处置在用户栈构造信号帧并改写被中断上下文（见下）。

由于这是唯一投递点，自投信号（如 `kill(getpid(), ...)`）在下一次返回用户态边界投
递，而非在 `SYS_KILL` 同步返回时投递。单核 PIT IRQ0 下，任何用户进程都会在一个
tick 内经过该边界，因此延迟有上界且确定。

## 信号帧与 sigreturn

对用户 handler 处置，`deliver_pending_to_user`：

1. 在被中断 `rsp` 之下计算帧地址，保留 System V red zone（128 字节）并 16 字节对
   齐。
2. 用 VMA-backed 可写用户缓冲校验（`validate_user_io_buffer`）与用户低半区上限校
   验帧范围。任何失败都确定性终止进程（等价默认 `SIGSEGV`），绝不写非法用户地
   址、不分配、不阻塞。
3. 把被中断的用户可见寄存器与 `rip`/`rsp`/`rflags`、信号号、handler 前阻塞掩码与
   一个 `SIGFRAME_MAGIC` 字保存进用户栈上的 `SignalFrame`。
4. 在 handler 期间把该信号加入阻塞掩码，然后改写被中断帧：`rip = handler`、
   `rdi = signo`（System V 第一参数）、`rsp = 帧地址`。

handler 末尾调用 `SYS_SIGRETURN`。`signal::sigreturn` 从 `rsp` 读取
`SignalFrame`，拒绝缺失/伪造的 magic 或位于用户低半区之外的 `rip`/`rsp`（确定性终
止），恢复用户可见寄存器与 `rip`/`rsp`，强制用户代码/数据段选择子与净化后的
`rflags`（IF 置位、保留位置位、特权位清零），并恢复 handler 前阻塞掩码（绝不重新
加入 `SIGKILL`）。它绝不依据用户可控数据返回内核特权上下文。本阶段无 libc
trampoline；smoke 用户程序（及任何 handler）须显式调用 `SYS_SIGRETURN`。

## 新增 syscall

`int 0x80` ABI 在 `SYS_GETGID = 15` 之后追加四个号，不改任何既有号位或寄存器约定
（号 -> rax，参数 -> rdi/rsi/rdx/r10/r8/r9，返回 -> rax）。均不发 i8259 EOI、不放宽
任何异常/IRQ 门或 DPL。

- `SYS_KILL = 16`（pid, signo）-> 0 或 `-ESRCH`/`-EPERM`/`-EINVAL`。
- `SYS_SIGACTION = 17`（signo, action, handler, old_out）-> 0 或 `-EINVAL`。对
  `SIGKILL` 设置 handler 或 ignore 返回 `-EINVAL`。
- `SYS_SIGPROCMASK = 18`（how, set, old_out）-> 0 或 `-EINVAL`。请求集合中的不可
  阻塞位被静默丢弃。
- `SYS_SIGRETURN = 19` 从用户栈信号帧恢复被中断用户上下文。它是唯一直接把被保存
  用户上下文（含 `rax`）改写回帧的 syscall，因此在共享 `rax` 回写前返回；不破坏
  `InterruptFrame` 布局约定。

`ESRCH` 与 `EPERM` 新增于 `include/bigos/errno.h`（单一来源，不在子系统源中重复定
义）。

## 验证

默认关闭的 `signal_smoke`（`xmake f --signal_smoke=y`，`BIGOS_SIGNAL_SMOKE`）发射
`BIGOS_SIGNAL_PASSED` / `BIGOS_SIGNAL_FAILED`。它覆盖固定编号/默认动作/`SIGKILL`
不可捕获+不可阻塞不变式、kill 置位 pending、阻塞掩码延迟再释放投递、用户 handler
投递构造用户栈帧且 `sigreturn` 精确恢复、以及越权 kill 被 `cred::may_signal` 拒绝。
既有 smoke 矩阵与默认启动 marker 不变。

与其他进程 smoke 一样，该默认关闭 smoke 在 init 运行前创建并拆除一个用户进程，因
此 smoke 构建随后会观察到 `BIGOS_INIT_LOAD_FAILED map-failed`；这是 smoke 模式产
物，非正常启动行为。`tests/` 下的源码契约/行为断言测试固定新增 syscall 号、
`Process` 信号字段、无分配投递路径、`SIGKILL` 不变式、kill 上的 `may_signal` 接
线，以及仅对用户态帧的 IRQ-return 投递点。

## 非目标与已知限制

本阶段非目标：实时信号（`SIGRTMIN`+ 排队）与完整 `siginfo`/`sigqueue`；
`SIGSTOP`/`SIGCONT` 作业控制、进程组与 `killpg`；`sigsuspend`/`sigpending`/
`sigaltstack` 与完整 `sigaction` 标志；`SA_RESTART` / EINTR syscall 重启（同步
syscall 不睡眠）；core dump 文件；用户态 libc 信号包装与自动 trampoline；以及 SMP
跨核投递。

已知限制：单一 IRQ-return 投递点下，自投信号在下一次返回用户态边界投递，而非在
`SYS_KILL` 同步返回时投递。
