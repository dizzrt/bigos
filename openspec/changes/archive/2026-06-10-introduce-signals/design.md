## Context

BigOS 当前没有任何信号机制：内核无法向进程投递异步通知，进程之间也不能终止彼此或自愿处理终止请求。已有的相关地基包括——

- 阶段 16.5 已在 `Process` 中加入 uid/gid/euid/egid，并实现了纯判定原语 `bigos::cred::may_signal(actor, target)`（见 [cred.cc](kernel/core/proc/cred.cc#L6-L16)），但目前没有任何强制点调用它。
- 进程生命周期已有 `exit`/`fault_current_and_exit`/zombie-to-reaper 的完整 teardown 路径，以及 `exit_code`/`fault_reason` 字段；fork/COW（阶段 16）与可增长进程/fd 表（阶段 15.5）已就位。
- IRQ dispatch（[interrupt.cc](kernel/core/irq/interrupt.cc#L132-L166)）在外部 IRQ handler 返回并发送 EOI 后，调用 `bigos::sched::maybe_preempt_on_irq_return(__frame)`；该路径已通过 `(__frame->cs & 0x3) == 0x3` 区分用户态/内核态被中断帧（见 page_fault_handler）。这正是 POSIX 信号「在返回用户态边界投递」的天然挂载点。
- syscall 走 `int 0x80`，号位固定、寄存器 ABI 冻结、向量 0x80 的 IDT gate 为 DPL=3 trap gate，syscall 路径不发 i8259 EOI。新增号只能在末尾追加。

约束：freestanding、单核、同步、无 libc；本阶段不提供用户态信号 trampoline，smoke 用户程序需自行 `int 0x80` 触发 `SYS_SIGRETURN`。信号帧构造与用户上下文改写是架构耦合的，需为「架构抽象」轨道预留隔离面。

## Goals / Non-Goals

**Goals:**

- 提供最小但正确的 POSIX 信号模型：固定信号编号集合、每信号默认动作、每进程 pending 位图 + 阻塞掩码 + 处置表，全部为 `Process` 内联定长字段，投递路径零分配。
- 在 `int 0x80` 末尾追加 `SYS_KILL`/`SYS_SIGACTION`/`SYS_SIGPROCMASK`/`SYS_SIGRETURN`，并把 `cred::may_signal` 接成 `SYS_KILL` 的实际权限强制点。
- 在 IRQ-return 到用户态的边界统一投递：默认 Terminate 走现有 exit/reaper 生命周期；用户 handler 在用户栈构造信号帧并改写返回上下文跳转 handler，由 `SYS_SIGRETURN` 恢复被中断的用户上下文。
- `SIGKILL` 不可捕获/不可阻塞；子进程退出时向父进程投递 `SIGCHLD`。
- 把架构耦合的信号帧构造/恢复隔离到一层薄内核接口，便于将来对接 arch trap-frame 接口。
- 全部能力经默认关闭的 `signal_smoke` 与源码契约/行为断言验证，失败语义确定性、无 panic（内核态 fault 除外）。

**Non-Goals:**

- 实时信号（`SIGRTMIN`+ 排队计数）、`sigqueue`/完整 `siginfo` 负载。
- `SIGSTOP`/`SIGCONT` 作业停止/恢复、进程组/会话、`killpg`。
- `sigsuspend`/`sigpending`/`sigaltstack`、`SA_RESTART` 等完整 `sigaction` 标志；可中断 syscall 的 EINTR 重启（本阶段同步 syscall 不睡眠，无需重启）。
- core dump 文件、用户态 libc 信号包装与自动 trampoline、SMP 跨核投递。
- 不改 `int 0x80` 寄存器 ABI、现有 syscall 号、IDT/向量/DPL、页表/CR3/地址布局、外部 IRQ/异常 EOI 语义。

## Decisions

### 决策 1：信号状态为 `Process` 内联定长字段，投递路径零分配

在 `Process` 追加 `uint64_t sig_pending`（pending 位图）、`uint64_t sig_mask`（阻塞掩码）、`SigDisposition sig_disp[SIG_COUNT]`（每信号处置：默认/忽略/handler 入口地址）。信号号取 1..N 的小固定集合（N ≤ 64），位图用 `1ull << (signo - 1)`。

- 理由：单核早期内核不需要实时信号的排队语义；位图天然「同种信号合并为一个 pending」，符合标准信号（非实时）语义，且 pending/mask 检查是热路径（每次 IRQ-return 到用户态都查），必须无分配、无锁、O(1)。处置表 N 个 entry 内联，fork 直接逐字段拷贝。
- 备选：(a) 每进程动态信号队列 + siginfo —— 引入投递路径分配与实时排队复杂度，超出最小目标，否决；(b) 全局信号表 —— 破坏每进程处置语义，否决。

### 决策 2：投递点 = IRQ-return 到用户态边界，复用现有钩子

新增 `signal::deliver_pending_to_user(InterruptFrame *frame, Process *p)`，在 `irq_dispatch` 的外部 IRQ 分支、`maybe_preempt_on_irq_return` 之后（或并入同一返回处理）调用，仅当 `(frame->cs & 0x3) == 0x3`（被中断帧是用户态）、当前进程存在、且 `sig_pending & ~sig_mask` 非空时处理。

- 理由：POSIX 语义要求信号在「即将返回用户态」时投递，避免在内核临界区运行用户 handler；该边界已被 timer 抢占钩子证明可安全地在 EOI 之后切换上下文，并已能区分用户/内核帧。集中在此一处投递可避免在每个 syscall 出口零散接线。
- syscall 出口的处理：`SYS_KILL` 给当前进程自投（如 `kill(getpid(), ...)`）的 pending，同样在最近一次返回用户态的 IRQ-return 边界统一投递；本阶段不在 syscall return 路径单独投递，保持单一投递点（见风险）。
- 备选：(a) 在每个 syscall dispatch 返回前投递 —— 多个投递点、易遗漏，否决；(b) 用软件中断专门触发投递 —— 增加新向量，超范围，否决。
- 数据流：`外部 IRQ -> handler -> EOI -> maybe_preempt_on_irq_return -> 若返回到用户态且有未阻塞 pending -> deliver_pending_to_user(frame, current) -> 选最低位 pending 信号 -> 默认动作? exit/reaper : 构造用户信号帧 + 改写 frame->rip/rsp 跳 handler -> iretq 进入 handler`。

### 决策 3：默认动作走现有 exit/fault-to-reaper 生命周期

默认动作只有两类：Terminate（`SIGKILL`/`SIGTERM`/`SIGSEGV`/`SIGUSR*` 默认等）与 Ignore（`SIGCHLD` 默认忽略）。Terminate 复用现有 `fault_current_and_exit`/exit 路径，把信号号编码进 `exit_code`/`fault_reason`（如约定 `128 + signo` 风格或专用编码），由现有 reaper 回收。

- 理由：信号「杀死进程」与现有 fault/exit teardown 是同一件事，复用可避免重复实现地址空间回收与 zombie 语义；`exit_code`/`fault_reason` 已是父进程 `wait` 的观察面。
- 失败行为：若投递时构造/终止失败（不应发生于默认动作），仍走确定性 kill，不 panic。
- 备选：信号自带独立 teardown —— 与生命周期重复，否决。

### 决策 4：用户 handler 在用户栈构造信号帧，`SYS_SIGRETURN` 恢复

捕获动作：在被中断帧的用户 `rsp` 之下（按 16 字节对齐、留 red zone）压入一个「信号帧」，至少包含被中断的用户寄存器上下文（足以恢复 `InterruptFrame` 的用户可见部分：rip/rsp/rflags/通用寄存器）与信号号；然后把 `frame->rip` 改为 handler 入口、`frame->rdi` 设为 signo（System V 第一参数）、`frame->rsp` 指向新帧。handler 执行完通过 `int 0x80` 调用 `SYS_SIGRETURN`，内核从用户栈上的信号帧恢复 `InterruptFrame` 并 iretq 回原用户上下文。投递时把该信号从 pending 清除，并在 handler 执行期间把该信号（及 `sa_mask` 简化为该信号自身）加入 mask，`SYS_SIGRETURN` 时恢复旧 mask。

- 理由：这是 Unix 信号交付的标准机制（内核改写返回上下文、用户栈承载 saved context、sigreturn 恢复）。把 saved context 放用户栈使其随进程地址空间天然隔离，无需内核侧每信号上下文存储。
- 失败行为：构造信号帧前用 VMA-backed 用户范围校验确认用户栈可写、不越界；不可写/越界 -> 确定性终止该进程（等价 `SIGSEGV` 默认），绝不在内核态写非法用户地址、绝不分配、绝不阻塞。
- 本阶段无 libc trampoline：smoke 用户程序的 handler 末尾自行 `int 0x80` 触发 `SYS_SIGRETURN`（不依赖自动返回地址）。明确记录这是 smoke 约定，阶段 19 libc 落地后再提供标准 `__restore_rt` trampoline。
- 备选：在内核态运行 handler —— 破坏 ring 边界与隔离，严重错误，否决；内核侧保存上下文表 —— 引入每进程上下文存储与生命周期，否决。

### 决策 5：`SYS_KILL` 用 `cred::may_signal` 强制权限，确定性 errno

`SYS_KILL(pid, signo)`：查找目标进程；不存在 -> `-ESRCH`；`cred::may_signal(current, target)` 拒绝 -> `-EPERM`；`signo` 非法 -> `-EINVAL`；否则把 `1<<(signo-1)` 置入目标 `sig_pending`（`SIGKILL`/不可阻塞信号忽略目标 mask）。投递发生在目标进程下次返回用户态时（单核下若目标非当前进程，待其被调度并 IRQ-return 时投递）。

- 理由：阶段 16.5 已实现且测试 `may_signal` 纯判定，本阶段只新增其唯一接线点，判定逻辑零改动，满足「升级强制点、不改语义」。
- 备选：`SYS_KILL` 自带权限逻辑 —— 与 cred 重复，否决。

### 决策 6：syscall 号紧随现有末尾追加

现有最大号是 `SYS_GETGID = 15`。新增：

```
SYS_KILL        = 16   // (pid, signo) -> 0 或 -ESRCH/-EPERM/-EINVAL
SYS_SIGACTION   = 17   // (signo, new_disp, old_disp_out) -> 0 或 -EINVAL
SYS_SIGPROCMASK = 18   // (how, new_set, old_set_out) -> 0 或 -EINVAL
SYS_SIGRETURN   = 19   // 无参数；从用户栈信号帧恢复被中断上下文，不正常"返回"
```

- ABI：沿用现有寄存器约定（号 -> rax，参数 -> rdi/rsi/rdx/...，返回值 -> rax），不发 EOI。`SYS_SIGRETURN` 是唯一会改写被保存用户上下文的 syscall：它从用户栈恢复 `InterruptFrame` 内容并经共享 `isr_common` iretq 路径返回，不破坏 frame 布局约定。
- 理由：只追加号位、不改既有号与寄存器布局，满足非破坏约束；一号一义符合现有风格。
- 备选：合并成一个带子命令的 syscall —— 偏离现有风格，否决。

### 决策 7：fork 继承处置/掩码、清空 pending；exec 重置 handler

- `fork_current`：子进程逐字段继承父进程 `sig_disp` 与 `sig_mask`；`sig_pending` 在子进程清空（POSIX：fork 子进程 pending 信号集为空）。
- `exec`：把所有「用户 handler」处置重置为默认（exec 替换镜像后旧 handler 地址失效），但保留 mask 与「被显式设为 Ignore」的处置（POSIX 语义近似）；本阶段为最小化，明确把 handler 一律重置默认、mask 保留、pending 保留。
- init/非 fork ELF 创建：`sig_disp` 全默认、`sig_mask` 空、`sig_pending` 空。
- 理由：与 POSIX fork/exec 信号语义对齐的最小子集；handler 入口是用户地址，exec 后必须失效，故重置默认是正确且必须的。
- 备选：fork 不继承 mask/disp —— 破坏 `fork`+handler 用例，否决。

### 决策 8：`SIGCHLD` 在子进程退出时投递给父进程

子进程进入 zombie（退出/被信号杀）时，向其父进程 `sig_pending` 置 `SIGCHLD` 位（默认动作 Ignore，但会作为「可被 handler 捕获」的事件）。不改变现有 `wait`/reaper 判定与 `wait_status_consumed`/`parent_waiting` 语义。

- 理由：`SIGCHLD` 是 shell（阶段 19）作业控制的基础事件；在已有的退出路径顺带置位，零额外分配。
- 备选：本阶段不做 `SIGCHLD` —— 但 shell 几乎必需，且接入成本极低（退出路径一处置位），故纳入；保持默认 Ignore 以不改变现有不捕获程序的行为。

### 决策 9：本阶段只保留单一 IRQ-return 投递点，不在 syscall 同步返回路径增加第二投递点

信号投递只在 IRQ-return 到用户态的边界发生（决策 2）；`SYS_KILL` 自投（如 `kill(getpid(), ...)`）产生的 pending 同样等待下一次返回用户态边界投递，本阶段**不**在 syscall dispatch 同步返回路径单独增加投递点。

- 理由：单核下 PIT IRQ0 周期性触发，任何用户进程都会很快（一个 tick 量级）经过 IRQ-return 边界，自投信号的投递延迟有上界且确定；单一投递点避免在每个 syscall 出口零散接线、降低 ABI 与上下文风险，并把「信号投递在下次返回用户态边界」立成明确且可测试的语义。明确不保证 syscall 同步返回即投递。
- 备选：(a) 在 syscall dispatch 返回前增加第二投递点 —— 多投递点易遗漏、扩大每个 syscall 出口的上下文/ABI 风险，且对当前同步、不睡眠的 syscall 收益有限，否决；(b) 用专门软件中断触发即时投递 —— 引入新向量，超出本阶段最小化目标，否决。
- 后续演进：若阶段 18/19 出现对自投信号即时性敏感的真实用例（如 `raise` 后立即期望 handler 已运行），再单独评估在 syscall 出口增加受控的第二投递点，复用同一 `deliver_pending_to_user`。

### 决策 10：exec 的信号语义固化为「handler 重置默认、mask 保留、pending 保留」

`exec` 用新镜像替换地址空间时：所有「用户 handler」处置一律重置为默认（旧 handler 入口是失效的用户地址，必须重置）；阻塞掩码 `sig_mask` 保留；pending 信号集 `sig_pending` 保留。

- 理由：handler 入口在 exec 后必然失效，重置默认是正确且必须的；保留 mask 与 pending 是 POSIX `execve` 信号语义的最小正确子集（execve 保留 pending 与 blocked set，仅把已捕获信号恢复为默认）。在当前无 `SIG_IGN` 跨 exec 特殊保留需求下，这是最贴近标准又最小的固化。
- 备选：(a) exec 清空 pending —— 偏离 POSIX 且会丢失 exec 前已投递的待处理信号，否决；(b) exec 同时清空 mask —— 偏离 POSIX `execve` 保留 blocked set 的语义，否决。
- 后续演进：若阶段 19 shell（`fork`+`exec`）对 exec 后 pending/`SIG_IGN` 保留有更精细期望，再在该阶段单独评估，不影响本阶段最小交付。

### 控制流总览

```
kill 路径:
  user int 0x80 SYS_KILL(pid, signo) -> dispatch:
     target = lookup(pid); 不存在 -> rax=-ESRCH
     !may_signal(current, target) -> rax=-EPERM
     signo 非法 -> rax=-EINVAL
     else: target->sig_pending |= bit(signo); rax=0

投递路径 (每次返回用户态):
  外部 IRQ -> handler -> EOI -> maybe_preempt_on_irq_return(frame)
     -> 若 (frame->cs&3)==3 且 current 有 (sig_pending & ~sig_mask):
        s = lowest_set; clear pending bit
        disp = current->sig_disp[s]
        Terminate(默认/或 SIGKILL) -> fault_current_and_exit(encode(s)) -> reaper
        Ignore -> 丢弃
        Handler -> 校验用户栈可写 -> push 信号帧(saved ctx + s)
                   -> frame->rip=handler, frame->rdi=s, frame->rsp=新帧
                   -> mask |= bit(s) (handler 期间)
        -> iretq 进入 handler

sigreturn 路径:
  user handler 末尾 int 0x80 SYS_SIGRETURN ->
     从用户栈信号帧恢复 frame(寄存器/rip/rsp/rflags), 恢复旧 mask
     -> iretq 回到信号前的用户上下文

子进程退出:
  exit/fault teardown -> parent->sig_pending |= bit(SIGCHLD) (不改 wait/reaper)
```

## Risks / Trade-offs

- [单一投递点（仅 IRQ-return）下，自投信号在无 IRQ 时延迟投递] → 单核下 PIT IRQ0 周期性触发，进程总会很快经过 IRQ-return 边界；记录此为「投递在下次返回用户态边界」的明确语义，不保证 syscall 同步返回即投递。如未来需要可在 syscall 出口增加第二投递点。
- [信号帧构造改写用户栈，越界/不可写会破坏用户进程或内核误写] → 构造前用 VMA-backed 用户范围校验（复用阶段 16.5/既有用户缓冲校验）确认可写与对齐；失败一律确定性终止目标进程，绝不在内核态写非法用户地址。
- [`SYS_SIGRETURN` 从用户栈恢复 `InterruptFrame`，恶意/损坏的信号帧可提权（改写 cs/rflags/rip 到内核）] → sigreturn 恢复时强制用户态约束：cs/ss 强制为用户段、rflags 强制 IF=1 且清除特权敏感位、rip/rsp 限制在用户低半区；不信任用户栈上的特权字段，只恢复用户可见的通用寄存器与受约束的 rip/rsp/rflags。
- [改写 `InterruptFrame` 破坏既有 iretq 返回约定或与 timer 抢占钩子顺序冲突] → 投递在 `maybe_preempt_on_irq_return` 之后、iretq 之前的单一明确顺序执行；实现前审查 `interrupt.s`/`interrupt.cc`/`switch.s` frame 布局，增加源码级 frame/顺序检查。
- [信号号集合/位图宽度过早固化] → 固定 N≤64 用 `uint64_t` 位图，预留高位；非实时信号合并语义明确记录，实时信号留作非目标。
- [架构耦合泄漏进通用信号逻辑] → 信号帧构造/恢复集中在 `signal::deliver_pending_to_user` / `signal::sigreturn` 两个函数，pending/mask/disp 管理与策略保持架构无关，便于将来对接 arch trap-frame 接口。
- [runtime smoke 在本地 emulator/toolchain 不可用] → validation 必须记录缺失工具、替代 source/build checks、跳过原因与残余 IRQ/syscall/用户栈风险。

## Migration Plan

- 纯增量：新增 `signal` 模块、`Process` 信号字段、4 个 syscall 分支、IRQ-return 投递点与 fork/exec/exit 接线；默认参与正常启动但不改变既有行为（无进程注册 handler 或被 kill 时，pending 恒空、投递路径直接跳过）。
- 默认动作 Terminate 复用既有 exit/reaper，不新增独立回收路径；`SIGCHLD` 仅在退出路径置位，默认 Ignore，不影响现有不捕获程序。
- 回滚：移除 `signal_smoke`、4 个 syscall 分支与 IRQ-return 投递点调用即可回到原状；`Process` 新增字段为追加，不影响既有布局假设。
- 验证开关 `signal_smoke` 默认关闭，不影响默认启动 marker 与既有 smoke 矩阵。

## Open Questions

- 无。原先的两个待定项已收敛为决策 9（本阶段只保留单一 IRQ-return 投递点，不在 syscall 同步返回路径增加第二投递点）与决策 10（exec 信号语义固化为「handler 重置默认、mask 保留、pending 保留」）。如阶段 18/19 对自投信号即时性或 exec 后 pending/`SIG_IGN` 保留有不同期望，再在对应阶段单独评估。
