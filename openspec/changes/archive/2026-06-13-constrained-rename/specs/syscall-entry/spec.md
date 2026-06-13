## ADDED Requirements

### Requirement: SYS_RENAME syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾以 append-only 方式新增受限 rename syscall（`SYS_RENAME` 或等价命名），接收源路径和目标路径两个用户态 NUL 终止字符串指针，返回值经 rax 写回。新增该 syscall MUST NOT 改变既有 syscall 号位、寄存器参数顺序、返回寄存器、`VECTOR_SYSCALL = 0x80`、DPL 设置、异常/IRQ/syscall 分离或 syscall 不发送 i8259 EOI 的规则。

#### Scenario: rename syscall 号追加在末尾

- **WHEN** 定义受限 rename syscall 号
- **THEN** 它 MUST 取当前 syscall ABI 末尾的新号位
- **AND** 既有 syscall 号、寄存器 ABI 和 no-EOI 语义 MUST 保持不变

#### Scenario: rename syscall 被 dispatch 路由

- **WHEN** 用户态以 rename syscall 号发起 `int 0x80` 并传入合法源路径和目标路径
- **THEN** dispatcher MUST 路由到 rename 实现并把 VFS 结果写回 rax
- **AND** syscall 路径 MUST NOT 发送 i8259 EOI 或放宽异常/外部 IRQ gate

### Requirement: rename syscall 校验用户路径和上下文

BigOS SHALL 在 rename syscall 调用 VFS 前验证并复制源路径和目标路径。路径读取 MUST 使用 VMA-backed 范围检查或等价 safe-copy 机制，MUST 校验用户地址、长度、NUL 终止和溢出；rename 在进入可能分配、等待或同步块 IO 的路径前 MUST 检查调度阻塞守卫。

#### Scenario: 两个 path 先复制到内核

- **WHEN** 用户态调用 rename syscall 并传入 `oldpath` 与 `newpath`
- **THEN** syscall 层 MUST 在调用 VFS 前验证并复制两个有界 NUL 终止 path
- **AND** 过长、未终止、内核地址、未映射或不可读 path MUST 被拒绝

#### Scenario: rename syscall 返回确定性负 errno

- **WHEN** rename 因非法用户路径、源缺失、目标已存在、只读后端、跨后端、权限、容量、fd/VFS 或后端 IO 失败
- **THEN** syscall MUST 通过返回寄存器返回确定性负 errno
- **AND** MUST NOT panic、发送 IRQ EOI、破坏当前进程 fd table 或改变 syscall ABI

#### Scenario: 不可阻塞上下文拒绝 rename

- **WHEN** rename syscall 逻辑在 IRQ、异常诊断、调度临界区或 preemption-disabled 不可阻塞上下文中被触达
- **THEN** BigOS MUST 拒绝该操作或进入文档化诊断路径
- **AND** MUST NOT 发布部分目录项、inode 变更或缓存写入
