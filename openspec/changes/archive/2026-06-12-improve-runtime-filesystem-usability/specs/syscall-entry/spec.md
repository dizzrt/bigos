## ADDED Requirements

### Requirement: 文件系统 syscall 支撑运行时可用性

BigOS SHALL 通过现有 `int 0x80` ABI 暴露有界运行时文件系统所需的用户态操作，包括文件打开、读取、写入、定位、同步、目录创建、最小目录枚举和删除。新增或扩展的文件相关 syscall MUST 保持 append-only 或既有号位语义扩展，MUST NOT 改变 `VECTOR_SYSCALL = 0x80`、寄存器参数顺序、返回寄存器、DPL 设置、异常/IRQ/syscall 分离或 syscall 不发送 i8259 EOI 的规则。

#### Scenario: ABI 不因运行时文件系统改变
- **WHEN** 文件相关 syscall 被新增或扩展
- **THEN** 既有 syscall 号位、寄存器 ABI、syscall vector 和 EOI 语义 MUST 保持不变
- **AND** 新能力 MUST 通过末尾追加号位或既有 open/write 语义扩展实现

#### Scenario: 文件 syscall 返回负 errno
- **WHEN** 文件相关 syscall 因路径、权限、容量、fd、用户缓冲或后端 IO 失败
- **THEN** syscall MUST 通过返回寄存器返回确定性负 errno
- **AND** MUST NOT panic、发送 IRQ EOI 或破坏当前进程 fd table

### Requirement: 文件 syscall 校验用户 path 和 buffer

BigOS SHALL 在文件相关 syscall 读取用户 path、读源 buffer、写目标 buffer、目录枚举输出 buffer、fd 数组或 argv/envp 组合数据前，使用 VMA-backed 范围检查或等价 safe-copy 机制验证用户地址、长度、NUL 终止和溢出。非法用户输入 MUST 返回确定性 `-EFAULT`、`-EINVAL` 或终止当前用户进程的文档化错误路径。

#### Scenario: open/mkdir/unlink path 先复制到内核
- **WHEN** 用户态传入 path 指针执行 `open`、`mkdir` 或 `unlink`
- **THEN** syscall 层 MUST 在调用 VFS 前验证并复制有界 NUL 终止 path
- **AND** 过长、未终止、内核地址、未映射或不可读 path MUST 被拒绝

#### Scenario: read/write buffer 方向正确
- **WHEN** 用户态执行 `read(fd, dst, len)` 或 `write(fd, src, len)`
- **THEN** `read` MUST 验证目标用户范围可写，`write` MUST 验证源用户范围可读
- **AND** 溢出范围、内核地址、只读目标或未映射页面 MUST 被拒绝

#### Scenario: 目录枚举输出 buffer 可写
- **WHEN** 用户态执行最小目录枚举 syscall 并传入输出 buffer
- **THEN** syscall 层 MUST 验证完整输出范围为用户可写且长度不超过有界上限
- **AND** 非目录 fd、非法 fd、过小 buffer 或非法用户地址 MUST 返回确定性负 errno

### Requirement: 文件 syscall 遵守阻塞上下文守卫

BigOS SHALL 在文件相关 syscall 进入可能分配、阻塞、等待或同步块 IO 的路径前检查调度阻塞守卫。普通用户进程 syscall MAY 进入 fd/VFS 和缓存路径；不可阻塞上下文 MUST 确定性失败或进入文档化诊断路径，MUST NOT 执行同步块 IO、等待或无界分配。

#### Scenario: 普通进程上下文可执行文件 IO
- **WHEN** 当前用户进程从普通 syscall 上下文执行文件操作
- **THEN** syscall 层 MAY 进入 fd/VFS、可写后端和缓存路径
- **AND** 返回值 MUST 经 syscall ABI 写回用户态

#### Scenario: 不可阻塞上下文拒绝文件 IO
- **WHEN** 文件 syscall 逻辑在 IRQ、异常诊断、调度临界区或 preemption-disabled 不可阻塞上下文中被触达
- **THEN** BigOS MUST 拒绝该操作或进入文档化诊断路径
- **AND** MUST NOT 发布部分 fd、目录项、inode 或缓存写入
