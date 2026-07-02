## ADDED Requirements

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
