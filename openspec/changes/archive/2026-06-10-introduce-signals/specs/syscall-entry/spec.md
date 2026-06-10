## ADDED Requirements

### Requirement: 信号相关 syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾新增信号相关 syscall（`SYS_KILL`/`SYS_SIGACTION`/`SYS_SIGPROCMASK`/`SYS_SIGRETURN`），紧随现有 `SYS_GETGID = 15` 之后固定号位，且不改变既有寄存器约定、既有 syscall 号、向量布局、DPL 设置或 syscall 不发 EOI 的规则。

#### Scenario: 信号 syscall 号紧随现有末尾

- **WHEN** 定义新的信号相关 syscall 号
- **THEN** 它们 MUST 从 `SYS_GETGID = 15` 之后连续编号（如 `SYS_KILL = 16`、`SYS_SIGACTION = 17`、`SYS_SIGPROCMASK = 18`、`SYS_SIGRETURN = 19`）
- **AND** 既有 syscall 号、寄存器 ABI（号 -> rax、参数 -> rdi/rsi/rdx/r10/r8/r9、返回值 -> rax）MUST 保持不变
- **AND** 这些 syscall MUST NOT 发送 i8259 EOI、MUST NOT 放宽任何异常或外部 IRQ 门

#### Scenario: SYS_KILL 投递并强制权限

- **WHEN** 用户态以 `SYS_KILL` 发起 `int 0x80`，传入目标 pid 与信号号
- **THEN** 分发器 MUST 路由到 kill 实现，对合法投递在目标进程置位 pending 并返回 0
- **AND** 对目标不存在返回 `-ESRCH`、对越权返回 `-EPERM`、对非法信号号返回 `-EINVAL`，全部经 rax 回写

#### Scenario: SYS_SIGACTION 注册或查询处置

- **WHEN** 用户态以 `SYS_SIGACTION` 发起 `int 0x80`，传入信号号与新处置（默认/忽略/handler 入口）
- **THEN** 分发器 MUST 更新当前进程对该信号的处置，并在请求时回写旧处置
- **AND** 对 `SIGKILL` 等不可捕获信号设置 handler 或忽略 MUST 返回 `-EINVAL`，对非法信号号 MUST 返回 `-EINVAL`

#### Scenario: SYS_SIGPROCMASK 修改阻塞掩码

- **WHEN** 用户态以 `SYS_SIGPROCMASK` 发起 `int 0x80`，传入操作类型与信号集合
- **THEN** 分发器 MUST 按操作类型修改当前进程阻塞掩码并在请求时回写旧掩码
- **AND** 试图阻塞 `SIGKILL` 等不可阻塞信号的位 MUST 被忽略而不报错或被拒绝为 `-EINVAL`（按文档化语义之一确定性处理）

#### Scenario: SYS_SIGRETURN 恢复被中断用户上下文

- **WHEN** 用户 handler 末尾以 `SYS_SIGRETURN` 发起 `int 0x80`
- **THEN** 分发器 MUST 从用户栈信号帧恢复用户可见寄存器、受约束的 rip/rsp/rflags 与旧阻塞掩码，并经现有 iretq 路径返回信号前的用户上下文
- **AND** 它 MUST NOT 破坏 `InterruptFrame` 字段布局约定，MUST NOT 依据用户栈数据返回到内核特权上下文，MUST NOT 发送 i8259 EOI
