## Purpose

定义 BigOS 最小 POSIX 信号能力：固定的信号编号集合与每信号默认动作；每进程 pending 位图、阻塞掩码与处置表（定长内联、信号投递路径无动态分配）；`kill`/`sigaction`/`sigprocmask`/`sigreturn` 系统调用；IRQ-return 到用户态边界的信号投递（默认动作走 exit/fault-to-reaper 生命周期，用户 handler 构造信号帧 + `sigreturn` 恢复）；`SIGKILL` 不可捕获/不可阻塞；基于 `bigos::cred::may_signal` 的「谁能 kill 谁」强制点；以及非法输入/越界用户栈的确定性失败语义与默认关闭验证开关。该能力以单核、同步、`int 0x80` 为前提，不引入实时信号、`siginfo` 完整负载、`SIGSTOP`/`SIGCONT` 作业控制、完整 `sigaction` 标志、进程组/会话、core dump、用户态 libc 信号包装或 SMP 跨核投递。
## Requirements
### Requirement: 信号编号集合与默认动作

BigOS SHALL 定义一组固定的最小 POSIX 信号编号（非实时、信号号合并语义），并为每个信号定义确定性的默认动作（Terminate 或 Ignore）。信号集合宽度 MUST 适配每进程定长位图（不超过 64 个），实时信号 MUST NOT 在本阶段引入。

#### Scenario: 固定信号编号被位图表示

- **WHEN** 内核或用户代码引用某个信号
- **THEN** 该信号 MUST 映射到一个固定编号，且能用 `1 << (signo - 1)` 在每进程定长位图中表示
- **AND** 同一信号在未投递前重复置位 MUST 合并为单个 pending 位（不做实时排队计数）

#### Scenario: 每信号有确定性默认动作

- **WHEN** 一个没有用户 handler 的信号被投递到进程
- **THEN** 内核 MUST 按该信号的默认动作处理（Terminate 类终止进程，Ignore 类丢弃）
- **AND** `SIGKILL` 的默认动作 MUST 为终止且 MUST NOT 可被覆盖

### Requirement: 每进程信号状态

BigOS SHALL 为每个进程维护信号 pending 位图、阻塞掩码与每信号处置表（默认/忽略/用户 handler 入口地址），全部为 `Process` 内联定长字段，且信号投递与查询路径 MUST NOT 进行动态内存分配。

#### Scenario: 进程携带 pending/mask/处置表

- **WHEN** 一个进程被创建
- **THEN** 它 MUST 拥有 pending 信号位图、阻塞掩码与每信号处置表字段
- **AND** 这些字段 MUST 为定长内联存储，信号投递路径 MUST NOT 分配内存或阻塞

#### Scenario: 阻塞掩码延迟投递

- **WHEN** 某信号在进程阻塞掩码中且被投递到该进程
- **THEN** 该信号 MUST 保持 pending 而不被立即处理
- **AND** 当掩码解除对该信号的阻塞后，下一次返回用户态边界 MUST 处理该 pending 信号
- **AND** `SIGKILL` MUST NOT 可被阻塞掩码延迟

### Requirement: kill 投递与权限强制

BigOS SHALL 提供向目标进程投递信号的内核路径，并以 `bigos::cred::may_signal` 作为「谁能向谁投递」的权限强制点，对非法目标、越权与非法信号号返回确定性错误。

#### Scenario: 合法 kill 置位目标 pending

- **WHEN** 调用方对一个存在且权限允许的目标进程投递一个合法信号
- **THEN** 内核 MUST 在目标进程 pending 位图置位对应信号
- **AND** 投递路径 MUST NOT 分配内存或阻塞

#### Scenario: 目标不存在返回 ESRCH

- **WHEN** 投递目标 pid 不对应任何活动进程
- **THEN** 内核 MUST 返回确定性负错误码 `-ESRCH`

#### Scenario: 越权投递返回 EPERM

- **WHEN** `bigos::cred::may_signal(actor, target)` 判定为拒绝
- **THEN** 内核 MUST 返回确定性负错误码 `-EPERM`
- **AND** 目标进程 pending 位图 MUST NOT 被修改

#### Scenario: 非法信号号返回 EINVAL

- **WHEN** 投递请求携带超出固定信号集合的信号号
- **THEN** 内核 MUST 返回确定性负错误码 `-EINVAL`

### Requirement: 信号在返回用户态边界投递

BigOS SHALL 在 IRQ-return 到用户态的边界投递未阻塞的 pending 信号，复用现有 IRQ-return 处理路径，且 MUST NOT 在内核态运行用户 handler，MUST NOT 在内核临界区投递。

#### Scenario: 仅对用户态被中断帧投递

- **WHEN** 外部 IRQ 处理结束并准备返回
- **THEN** 内核 MUST 仅在被中断帧是用户态（`cs & 0x3 == 3`）且当前进程存在未阻塞 pending 信号时处理投递
- **AND** 被中断帧为内核态时 MUST NOT 投递用户信号

#### Scenario: 默认终止动作走生命周期回收

- **WHEN** 投递的信号处置为默认 Terminate（或为 `SIGKILL`）
- **THEN** 内核 MUST 通过现有 exit/fault-to-reaper 生命周期终止该进程
- **AND** MUST 把信号号编码进进程退出/fault 状态供父进程 `wait` 或诊断观察

#### Scenario: 忽略动作丢弃信号

- **WHEN** 投递的信号处置为 Ignore
- **THEN** 内核 MUST 清除该 pending 位并继续返回用户态，不改变用户上下文

### Requirement: 用户 handler 信号帧与 sigreturn 恢复

BigOS SHALL 在投递可捕获信号时于用户栈上构造信号帧并改写返回上下文跳转到用户 handler，并提供 `sigreturn` 路径从用户栈恢复被中断的用户上下文。恢复时 MUST 强制用户态约束，MUST NOT 信任用户栈上的特权字段。

#### Scenario: 捕获信号构造用户栈帧并跳转 handler

- **WHEN** 投递的信号处置为用户 handler 且用户栈范围经校验可写、对齐、不越界
- **THEN** 内核 MUST 在用户栈上保存被中断的用户可见上下文与信号号，并把返回 rip 改为 handler 入口、第一参数寄存器设为信号号、rsp 指向新帧
- **AND** 内核 MUST 在 handler 执行期间把该信号加入阻塞掩码，并在投递前清除其 pending 位

#### Scenario: 用户栈不可写时确定性终止

- **WHEN** 构造信号帧所需的用户栈范围不可写、越界或非用户映射
- **THEN** 内核 MUST 确定性终止该进程（等价默认 Terminate）
- **AND** MUST NOT 在内核态写非法用户地址、MUST NOT 分配或阻塞

#### Scenario: sigreturn 恢复并强制用户态约束

- **WHEN** 用户 handler 通过 `sigreturn` 返回内核
- **THEN** 内核 MUST 从用户栈信号帧恢复用户可见寄存器、受约束的 rip/rsp/rflags 与旧阻塞掩码
- **AND** 内核 MUST 强制段寄存器为用户段、rflags 保持 IF 置位且不提升特权敏感位、rip/rsp 限制在用户地址空间
- **AND** 内核 MUST NOT 依据用户栈数据返回到内核特权上下文

### Requirement: 信号子系统验证可复现

BigOS SHALL 通过默认关闭的 `signal_smoke`（`BIGOS_SIGNAL_SMOKE`）与源码契约/行为断言验证信号能力，且不改变默认启动 marker 与既有 smoke 矩阵。

#### Scenario: signal smoke 发射有界判定 marker

- **WHEN** 启用 `signal_smoke` 构建并在模拟器中启动
- **THEN** 验证 MUST 覆盖「kill 默认动作终止目标」「用户 handler 捕获 + sigreturn 恢复」「掩码阻塞 pending、解除后投递」「`SIGKILL` 不可捕获/不可阻塞」「越权 kill 被拒绝」并发射有界判定 marker（如 `BIGOS_SIGNAL_PASSED`/`BIGOS_SIGNAL_FAILED`）
- **AND** 该开关 MUST 默认关闭，默认启动 marker 与既有 smoke 矩阵 MUST 保持不变

### Requirement: User signal handler return path
BigOS SHALL provide a bounded user signal handler return path that routes handler completion through a libc-owned trampoline and `SYS_SIGRETURN`, restoring the interrupted user context or failing through the existing diagnostic path when the signal frame is invalid.

#### Scenario: Handler returns to interrupted context
- **WHEN** a process installs a handler for a supported signal and the handler returns normally
- **THEN** control transfers through the signal trampoline to `SYS_SIGRETURN` and resumes the interrupted user context with the expected signal mask state

#### Scenario: Invalid signal frame fails safely
- **WHEN** `SYS_SIGRETURN` observes an invalid or unmapped user signal frame
- **THEN** the kernel rejects the return through the existing bounded process-failure behavior instead of continuing with corrupted register state

### Requirement: Signal wrapper and kernel contract alignment
BigOS SHALL keep the user `sigaction` and `sigprocmask` data layout aligned with the kernel signal dispatch contract, including supported signal numbers, handler disposition, mask updates, and old-action or old-mask outputs.

#### Scenario: sigaction reports old disposition
- **WHEN** a user program installs a new disposition and requests the previous one
- **THEN** the old disposition returned to user space reflects the process state before the update

#### Scenario: sigprocmask reports old mask
- **WHEN** a user program updates the signal mask and requests the previous mask
- **THEN** the old mask returned to user space reflects the process state before the update

