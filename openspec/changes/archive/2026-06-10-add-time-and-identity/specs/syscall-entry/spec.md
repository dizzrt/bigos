## ADDED Requirements

### Requirement: 只读身份与时间查询 syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾新增只读的身份/时间查询 syscall（`SYS_GET_TIME`/`SYS_GETPID`/`SYS_GETPPID`/`SYS_GETUID`/`SYS_GETGID`），紧随现有 `SYS_FORK = 10` 之后固定号位，返回值经 rax 回写，且不改变既有寄存器约定、既有 syscall 号或 EOI 语义。

#### Scenario: 新增号位紧随现有末尾

- **WHEN** 定义新的身份/时间查询 syscall 号
- **THEN** 它们 MUST 从 `SYS_FORK = 10` 之后连续编号（如 `SYS_GET_TIME = 11`、`SYS_GETPID = 12`、`SYS_GETPPID = 13`、`SYS_GETUID = 14`、`SYS_GETGID = 15`）
- **AND** 既有 syscall 号、寄存器 ABI（号 -> rax、返回值 -> rax）MUST 保持不变

#### Scenario: SYS_GET_TIME 返回当前墙钟秒

- **WHEN** 用户态以 `SYS_GET_TIME` 发起 `int 0x80`
- **THEN** 分发器 MUST 把当前墙钟 Unix 秒写入 rax 返回
- **AND** 该调用 MUST NOT 发送 i8259 EOI、阻塞或分配内存

#### Scenario: 身份查询返回当前进程字段

- **WHEN** 用户态以 `SYS_GETPID`/`SYS_GETPPID`/`SYS_GETUID`/`SYS_GETGID` 发起 `int 0x80`
- **THEN** 分发器 MUST 分别把当前进程的 pid/parent_pid/uid/gid 写入 rax 返回
- **AND** 这些调用 MUST 为只读、MUST NOT 发送 i8259 EOI、阻塞或分配内存
