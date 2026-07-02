## Purpose

定义 BigOS 内核单一错误码来源 `include/bigos/errno.h` 的契约：集中定义当前在用的
POSIX 风格 errno 常量，规定错误码命名、数值稳定性、按惯例取负写入返回寄存器的约定，
以及「禁止子系统再各自定义重复错误码常量」的收敛要求。本能力为纯机械的符号收敛，
不改变任何错误码数值、错误语义集合或 syscall ABI，且不覆盖调度器与驱动层的非 POSIX
错误表达。

## Requirements

### Requirement: 单一错误码来源头文件

BigOS SHALL 提供单一错误码来源头文件 `include/bigos/errno.h`，集中定义内核当前在用的
POSIX 风格错误码常量。该头文件 MUST 位于 `bigos` 命名空间，MUST 保持最小依赖且
freestanding-safe（仅整型编译期常量，不依赖 libc 或用户态运行时）。其它子系统头文件
MUST NOT 再各自定义重复的错误码常量值，而 MUST 引用此单一来源。

#### Scenario: 错误码集中定义于 errno.h

- **WHEN** 内核代码需要表达 `EBADF`、`EWOULDBLOCK`、`EINVAL`、`EMFILE`、`EFAULT`、
  `ENOSYS`、`ECHILD` 等错误码
- **THEN** 这些错误码 MUST 仅在 `include/bigos/errno.h` 中有唯一权威定义
- **AND** 任何其它头文件或源文件 MUST NOT 为同一错误码引入第二个独立的数值定义

#### Scenario: 头文件 freestanding-safe

- **WHEN** 在 freestanding 内核构建中包含 `bigos/errno.h`
- **THEN** 它 MUST 仅暴露编译期整型错误码常量
- **AND** MUST NOT 依赖 libc、用户态 `errno` 全局变量或任何 OS 服务

### Requirement: 错误码数值稳定且与现状一致

错误码收敛 MUST 为纯机械的符号收敛：所有错误码的数值与可观察行为（写入返回寄存器的
负值）MUST 与收敛前完全一致。收敛过程 MUST NOT 改变任何错误码数值、MUST NOT 新增或
删除错误语义、MUST NOT 修改 syscall ABI。

#### Scenario: 收敛后返回寄存器数值不变

- **WHEN** syscall 或 fd/VFS/wait 路径返回某个错误码
- **THEN** 返回寄存器中的负值数值 MUST 与收敛前一致（如 `EBADF` 对应 -9、
  `EWOULDBLOCK` 对应 -11、`EINVAL` 对应 -22、`EMFILE` 对应 -24、`EFAULT` 对应 -14、
  `ENOSYS` 对应 -38、`ECHILD` 对应 -10）

#### Scenario: 不改变错误语义集合

- **WHEN** 对比收敛前后的错误码集合
- **THEN** 错误语义的种类 MUST 不增不减，仅符号名与定义位置发生收敛

### Requirement: 子系统重复错误码收敛到单一来源

子系统重复错误码 MUST 收敛到单一来源：`include/bigos/syscall.h`、
`include/bigos/proc.h` 中现有的 `SYS_E*`、`FD_E*`、`WAIT_E*` 重复错误码常量
MUST 改为引用 `bigos/errno.h` 的单一来源；冗余的按子系统前缀的独立数值定义
MUST 被移除。错误码写入返回寄存器时按惯例取负的约定 MUST 保持不变。

#### Scenario: 重复错误码不再各自独立定义

- **WHEN** 检索 `EBADF`、`EWOULDBLOCK`、`EINVAL`、`EMFILE` 等在多个子系统中出现的
  错误码
- **THEN** 它们 MUST 解析到 `bigos/errno.h` 的单一定义
- **AND** MUST NOT 残留诸如 `SYS_EBADF` 与 `FD_EBADF` 同时独立定义同一数值的重复声明

#### Scenario: 取负写入返回寄存器的惯例保持不变

- **WHEN** 内核把统一错误码写入 syscall 返回寄存器
- **THEN** 它 MUST 按既有惯例取负后写入
- **AND** 返回的负值数值 MUST 与收敛前相同

### Requirement: 非 POSIX 错误表达不在收敛范围

错误码收敛 MUST 仅覆盖 POSIX 风格 errno。调度器的 `WAIT_TIMEOUT`、
`WAIT_BLOCK_FORBIDDEN` 与 driver 层的 `BlockStatus` 枚举等非 POSIX 错误表达
MUST NOT 被纳入本次收敛，其定义与语义 MUST 保持不变。

#### Scenario: 调度与驱动错误表达保持独立

- **WHEN** 收敛 POSIX 风格 errno
- **THEN** `sched` 的 `WAIT_TIMEOUT`/`WAIT_BLOCK_FORBIDDEN` 与 driver 的
  `BlockStatus` 枚举 MUST 保持原有定义与数值不变
- **AND** MUST NOT 被改写为引用 `bigos/errno.h`

### Requirement: 面向连接语义的错误码常量

BigOS SHALL 在单一错误码来源头文件 `include/bigos/errno.h` 中以 append-only 方式新增面向连接（stream socket / TCP）语义所需的 POSIX 风格错误码常量，至少包含 `ECONNREFUSED`、`ECONNRESET`、`EISCONN`、`ENOTCONN`、`EINPROGRESS`、`EALREADY` 与 `ENOPROTOOPT`（受限 `getsockopt` 未支持 option）。新增常量的数值 MUST 对齐既有约定（Linux x86_64 惯用取值），MUST 保持 freestanding-safe（仅编译期整型常量），并遵循既有「取负写入返回寄存器」约定。新增 MUST NOT 改变任何既有错误码的取值或语义，其它子系统头文件 MUST NOT 为这些错误码引入第二个独立定义。

#### Scenario: 连接错误码集中定义于 errno.h

- **WHEN** 内核 stream socket / TCP 路径需要表达连接被拒绝、连接被复位、已连接、未连接、连接进行中、已在连接或未支持的 socket option
- **THEN** `ECONNREFUSED`、`ECONNRESET`、`EISCONN`、`ENOTCONN`、`EINPROGRESS`、`EALREADY`、`ENOPROTOOPT` MUST 仅在 `include/bigos/errno.h` 中有唯一权威定义
- **AND** 任何其它头文件或源文件 MUST NOT 为同一错误码引入第二个独立数值定义

#### Scenario: 新增不改变既有错误码

- **WHEN** 对比新增前后的错误码集合
- **THEN** 既有错误码的数值与可观察行为（写入返回寄存器的负值）MUST 完全不变
- **AND** 新增仅以 append-only 方式扩展面向连接语义所需常量

#### Scenario: 新增常量 freestanding-safe

- **WHEN** 在 freestanding 内核构建中包含 `bigos/errno.h`
- **THEN** 新增连接错误码 MUST 仅为编译期整型常量
- **AND** MUST NOT 依赖 libc、用户态 `errno` 全局变量或任何 OS 服务
