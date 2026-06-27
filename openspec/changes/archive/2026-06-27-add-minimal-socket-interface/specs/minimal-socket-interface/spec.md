## ADDED Requirements

### Requirement: 最小 UDP socket 创建与 fd 集成
BigOS SHALL provide a user-visible minimal UDP socket that is represented as a kernel-internal `vfs::File` backend and installed into the existing per-process fd table. socket 创建 MUST 校验请求的 domain/type/protocol 属于 BigOS 有界 UDP 子集，成功时返回一个进程本地 fd，并复用既有 fd 分配、`dup`/`dup2`、`close` 与 `close-on-exec` 路径。该 socket fd MUST NOT 改变既有 syscall number、pipe、VFS 或文件 fd 的行为。

#### Scenario: 创建有界 UDP socket
- **WHEN** 用户程序以 BigOS 有界 UDP 子集（IPv4 datagram）参数调用 socket 创建 syscall，且 fd 表与协议端口表均有可用容量
- **THEN** BigOS MUST 创建一个 socket backend `vfs::File`，把它安装到最低可用 fd 并返回该 fd
- **AND** 该 fd MUST 可被既有 `close`、`dup`、`dup2`、`fcntl`（`close-on-exec`）路径处理

#### Scenario: 拒绝不支持的 socket 参数
- **WHEN** 用户程序请求超出 BigOS 有界 UDP 子集的 domain、type 或 protocol（例如 stream/TCP、未支持的地址族）
- **THEN** BigOS MUST 以确定性负 errno 拒绝创建，且 MUST NOT 安装任何 fd

#### Scenario: 容量耗尽
- **WHEN** 进程 fd 表已达上限或协议层 UDP 端口/资源已无可用容量
- **THEN** BigOS MUST 返回确定性负 errno（fd 表满与协议资源满使用各自确定性的错误码）
- **AND** 失败路径 MUST NOT 泄漏部分创建的 `vfs::File` 或内核内部 endpoint

### Requirement: socket 本地端口绑定
BigOS SHALL provide a bind operation that binds a UDP socket fd to a local port through the kernel-internal `bigos::net` UDP API within bounded semantics。bind MUST 校验 fd 是 socket、用户提供的定长地址结构与声明长度一致，并把协议层结果（成功、重复绑定、端口表满等）映射为确定性返回值。

#### Scenario: 绑定本地端口成功
- **WHEN** 用户程序对一个未绑定的 UDP socket fd 提供合法的定长本地地址结构并请求 bind
- **THEN** BigOS MUST 通过内核内部 UDP API 绑定该本地端口并返回成功
- **AND** 该 socket 随后 MUST 能接收发往该本地端口的 datagram

#### Scenario: 拒绝非法地址或非 socket fd
- **WHEN** bind 的 fd 不是 socket、地址结构长度与约定定长不一致，或地址字段非法
- **THEN** BigOS MUST 返回确定性负 errno，且 MUST NOT 改变该 fd 或协议层绑定状态

#### Scenario: 重复绑定或端口表满
- **WHEN** 目标本地端口已被占用、该 socket 已绑定，或协议层 UDP 端口表已满
- **THEN** BigOS MUST 返回确定性负 errno（重复绑定与资源满使用各自确定性错误码）

### Requirement: socket datagram 发送
BigOS SHALL provide a sendto operation that transmits a bounded UDP payload to a destination IPv4 address and port through the kernel-internal `bigos::net` UDP API。sendto MUST 通过既有用户缓冲校验/拷贝路径读取 payload 与目的地址，payload 长度 MUST 受 `UDP_MAX_PAYLOAD`/`SYS_IO_MAX_LEN` 上限约束，并把 ARP 未解析、无路由、过大、超时、设备发送失败等协议层结果映射为确定性返回值。该操作 MUST 在 ordinary（可阻塞）内核上下文执行，MUST NOT 从 IRQ 上下文调用协议层。

#### Scenario: 发送 datagram 成功
- **WHEN** 用户程序对一个 socket fd 提供合法目的地址与不超过上限的 payload 并请求 sendto
- **THEN** BigOS MUST 校验并拷贝用户 payload 与目的地址，调用内核内部 UDP 发送，并返回已发送字节数或确定性成功结果

#### Scenario: 拒绝过大或非法发送
- **WHEN** payload 超过 `UDP_MAX_PAYLOAD`/`SYS_IO_MAX_LEN` 上限、用户缓冲或地址指针无法通过 VMA-backed 校验，或目的地址结构长度非法
- **THEN** BigOS MUST 返回确定性负 errno，且 MUST NOT 提交越界或未校验的数据到协议层

#### Scenario: 协议层发送失败映射
- **WHEN** 内核内部 UDP 发送返回 ARP 未解析、无路由、too-large、timeout 或设备发送失败
- **THEN** BigOS MUST 把该结果映射为确定性负 errno 返回给用户，且 MUST NOT 声称发送成功

### Requirement: socket datagram 接收
BigOS SHALL provide a recvfrom operation that returns one received UDP datagram together with its source IPv4 address and port, built on the protocol layer 非阻塞 `udp_receive_from` 与显式 `pump` 推进。recvfrom MUST 在 ordinary 内核上下文运行，采用有界的 RX 推进与有界等待/无数据语义（非通用 POSIX 阻塞），并通过既有 copy-to-user 路径把来源地址与 payload 写回用户缓冲。

#### Scenario: 接收 datagram 成功
- **WHEN** 一个已绑定本地端口的 socket fd 调用 recvfrom，且协议层在有界推进后存在该端口的待收 datagram
- **THEN** BigOS MUST 把 payload（按用户缓冲长度有界截断或拒绝）与来源 IPv4/port 写回用户提供的缓冲与定长地址结构
- **AND** 返回已接收字节数或确定性成功结果

#### Scenario: 无数据的有界返回
- **WHEN** socket fd 调用 recvfrom，但经过有界 RX 推进/等待后仍无可用 datagram
- **THEN** BigOS MUST 以确定性方式返回无数据结果（确定性负 errno），且 MUST NOT 无界忙等独占 CPU 或永久阻塞
- **AND** 该有界、非通用 POSIX 阻塞语义 MUST 被明确为本能力边界

#### Scenario: 拒绝未绑定或非法缓冲接收
- **WHEN** recvfrom 的 fd 不是 socket、socket 未绑定本地端口，或用户缓冲/地址输出指针无法通过 VMA-backed 校验
- **THEN** BigOS MUST 返回确定性负 errno，且 MUST NOT 写入未校验的用户内存

### Requirement: socket fd 生命周期与 read/write 语义
BigOS SHALL integrate socket fds with the existing fd lifecycle，使 socket 在 `close`、`fork`、`close-on-exec` 与引用计数路径下行为确定。socket 的 `read`/`write` ops MUST 不充当隐式无地址收发：在无连接 UDP 语义下，对 socket fd 的 `read`/`write` MUST 返回确定性不支持错误，收发只通过 `sendto`/`recvfrom`。

#### Scenario: 关闭释放 socket 资源
- **WHEN** socket fd 的最后一个引用被 `close`（或进程退出统一回收）
- **THEN** BigOS MUST 通过既有 `vfs::release` 路径调用 socket backend 的 close，回收对应内核内部 endpoint，且 MUST NOT 重复释放或泄漏

#### Scenario: fork 后共享 socket 引用
- **WHEN** 持有 socket fd 的进程 `fork`
- **THEN** 子进程 MUST 通过既有 `vfs::retain` 引用计数共享同一 socket `vfs::File`/endpoint
- **AND** 任一进程关闭其 fd MUST 只递减引用计数，仅在最后一次释放时回收 endpoint

#### Scenario: socket read/write 返回不支持
- **WHEN** 用户程序对 socket fd 直接调用 `read` 或 `write` 系统调用
- **THEN** BigOS MUST 返回确定性不支持错误码，且 MUST NOT 进行隐式网络收发

### Requirement: socket syscall 与用户 libc 边界
BigOS SHALL expose the socket operations as a bounded set of new syscalls numbered after the current highest syscall，并在内核与用户两份 syscall number 定义中保持一致。变更 MUST NOT 修改既有 syscall number 的取值或语义，并 MUST 在用户 libc 中提供最小 socket wrapper，保持 freestanding-safe 且使用既有 errno 翻译。

#### Scenario: 新增 syscall number 双份一致
- **WHEN** 构建内核与用户态并运行既有 syscall number 源契约校验
- **THEN** 新增的 socket 系列 number MUST 在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 中相等
- **AND** 既有 syscall number 的取值与语义 MUST 保持不变

#### Scenario: 用户 libc socket wrapper 可用
- **WHEN** freestanding 用户程序链接本仓库有界 libc 并调用 socket/bind/sendto/recvfrom wrapper
- **THEN** wrapper MUST 通过既有 `int 0x80` ABI 传参并把内核负返回翻译为 errno
- **AND** wrapper MUST NOT 依赖 hosted libc、异常、RTTI 或非 freestanding 运行时

### Requirement: 默认关闭验证与默认启动独立性
BigOS SHALL validate the minimal socket interface through a default-off smoke that exercises socket 创建、bind、sendto/recvfrom 闭环与错误路径，并保证默认启动在无网络后端时不依赖 socket 能力。验证 MUST 提供可在无真实 tap/网络后端环境运行的内核内部/注入式闭环路径；真实网络后端不可用时 MUST 记录跳过原因与剩余风险。

#### Scenario: socket smoke 闭环通过
- **WHEN** 启用 socket 验证 build switch 并在受控环境运行 socket smoke
- **THEN** smoke MUST 覆盖 socket 创建、bind、sendto/recvfrom 闭环与错误路径（未 bind、地址非法、缓冲越界、资源满、无数据），并发出确定性通过/失败 marker

#### Scenario: 默认启动不依赖 socket
- **WHEN** socket 验证 switch 关闭或无网络后端
- **THEN** 默认启动、storage、filesystem、`/rw`、shell 与 userland baseline MUST 保持与 socket 能力无关并正常进入 shell

#### Scenario: 验证不可用时记录跳过
- **WHEN** QEMU、tap 权限、MSI-X、串口捕获或 x86_64-elf 工具链等验证依赖不可用
- **THEN** 验证记录 MUST 区分已通过、无法运行（含原因与剩余风险）与历史诊断，且 MUST NOT 声称运行成功
