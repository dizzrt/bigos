## ADDED Requirements

### Requirement: 可写 I/O 与管道相关 syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾新增可写 I/O 与管道相关 syscall（`SYS_LSEEK`/`SYS_PIPE`/`SYS_DUP`/`SYS_DUP2`/`SYS_FSYNC`/`SYS_MKDIR`/`SYS_UNLINK`），紧随现有 `SYS_SIGRETURN = 19` 之后固定号位，且 MUST NOT 改变既有寄存器约定、既有 syscall 号、向量布局、DPL 设置或 syscall 不发 EOI 的规则。涉及分配、同步块 IO 或阻塞的 syscall MUST 在进入前检查调度阻塞守卫。

#### Scenario: 新 syscall 号紧随现有末尾

- **WHEN** 定义新的可写 I/O 与管道相关 syscall 号
- **THEN** 它们 MUST 从 `SYS_SIGRETURN = 19` 之后连续编号（如 `SYS_LSEEK = 20`、`SYS_PIPE = 21`、`SYS_DUP = 22`、`SYS_DUP2 = 23`、`SYS_FSYNC = 24`、`SYS_MKDIR = 25`、`SYS_UNLINK = 26`）
- **AND** 既有 syscall 号、寄存器 ABI（号 -> rax、参数 -> rdi/rsi/rdx/r10/r8/r9、返回值 -> rax）MUST 保持不变
- **AND** 这些 syscall MUST NOT 发送 i8259 EOI、MUST NOT 放宽任何异常或外部 IRQ 门

#### Scenario: SYS_PIPE 创建管道对

- **WHEN** 用户态以 `SYS_PIPE` 发起 `int 0x80`，传入用户侧两元 fd 数组指针
- **THEN** 分发器 MUST 路由到管道创建实现，成功时把读/写端两个 fd 写回用户数组并返回 0
- **AND** 对 fd 表不足返回 `-EMFILE`、内存不足返回 `-ENOMEM`、用户指针非法返回 `-EFAULT`，全部经 rax 回写

#### Scenario: SYS_DUP/SYS_DUP2 复制 fd

- **WHEN** 用户态以 `SYS_DUP`（传 oldfd）或 `SYS_DUP2`（传 oldfd、newfd）发起 `int 0x80`
- **THEN** 分发器 MUST 返回新 fd 并令其与 oldfd 共享同一打开文件对象；`SYS_DUP2` 在 newfd 已打开时 MUST 先关闭它
- **AND** 对非法 fd 返回 `-EBADF`、对无可用 fd 返回 `-EMFILE`

#### Scenario: SYS_LSEEK 定位

- **WHEN** 用户态以 `SYS_LSEEK` 发起 `int 0x80`，传入 fd、offset、whence
- **THEN** 分发器 MUST 返回新的绝对 offset；对非法 fd 返回 `-EBADF`、对管道返回 `-ESPIPE`、对非法 whence 或溢出返回 `-EINVAL`

#### Scenario: SYS_FSYNC 落盘

- **WHEN** 用户态以 `SYS_FSYNC` 发起 `int 0x80`，传入一个指向可写文件的 fd
- **THEN** 分发器 MUST 在检查阻塞守卫后把该文件相关脏块经块缓冲缓存落盘，成功返回 0
- **AND** 对非法 fd 返回 `-EBADF`、对块 IO 失败返回 `-EIO`

#### Scenario: SYS_MKDIR/SYS_UNLINK 目录变更

- **WHEN** 用户态以 `SYS_MKDIR`（path、mode）或 `SYS_UNLINK`（path）发起 `int 0x80`
- **THEN** 分发器 MUST 在检查阻塞守卫与访问权限后执行目录项创建/删除，成功返回 0
- **AND** 对只读后端返回 `-EROFS`、权限拒绝返回 `-EACCES`、空间耗尽返回 `-ENOSPC`，并按目标状态返回 `-EEXIST`/`-ENOENT`/`-EISDIR`/`-EINVAL`

## MODIFIED Requirements

### Requirement: SYS_OPEN 与 SYS_WRITE 语义扩展为可写 I/O

BigOS SHALL 扩展既有 `SYS_OPEN` 与 `SYS_WRITE` 的语义以支持可写 I/O，但 MUST NOT 改变其 syscall 号位（`SYS_OPEN = 5`、`SYS_WRITE = 2`）、寄存器 ABI 或「syscall 不发 EOI」规则。`SYS_OPEN` MUST 接受可写/创建 flags（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）与 `O_CREAT` 的 mode；`SYS_WRITE` MUST 支持写入文件或管道 fd 而不再仅限控制台 fd，并在分配或进入同步块 IO/阻塞前检查调度阻塞守卫。现有只读 open 与控制台 write 行为 MUST 保持不变。

#### Scenario: SYS_OPEN 接受可写/创建 flags

- **WHEN** 用户态以 `SYS_OPEN` 传入可写或 `O_CREAT` flags 与（创建时的）mode 打开可写后端的路径
- **THEN** 分发器 MUST 在权限判定通过后返回一个可写打开文件的进程 fd
- **AND** 对只读后端的写打开返回 `-EROFS`、权限拒绝返回 `-EACCES`、空间耗尽返回 `-ENOSPC`，且只读 flags 的既有打开语义保持不变

#### Scenario: SYS_WRITE 写入文件或管道 fd

- **WHEN** 用户态以 `SYS_WRITE` 向一个指向可写文件或管道写端的 fd 写入有界用户缓冲
- **THEN** 分发器 MUST 把数据写入对应打开文件对象（文件经块缓冲缓存、管道经环形缓冲），返回实际写入字节数
- **AND** 对非法/不可写 fd 返回 `-EBADF`、对读端全关的管道返回 `-EPIPE`、对无空间返回 `-ENOSPC`，且既有控制台 fd 写行为保持不变

#### Scenario: 既有只读与控制台路径不变

- **WHEN** 用户态以只读 flags `SYS_OPEN` 或向控制台 fd `SYS_WRITE`
- **THEN** 其行为 MUST 与扩展前完全一致，号位与寄存器 ABI MUST 保持不变
