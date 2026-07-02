## Purpose

定义 BigOS 用户可见的最小有界 TCP stream socket 接口：在既有 fd 表、syscall 路径、内核内部 TCP 路径与统一 fd 就绪模型之上，提供 stream socket 创建、connect、listen/accept、read/write/send、受限 getsockopt(SO_ERROR)、非阻塞与 poll 集成、生命周期、用户 libc 边界和默认关闭验证。该能力限定为 IPv4 TCP stream socket 的有界子集，不声称完整 POSIX socket 语义、完整 TCP 特性矩阵、名字解析或通用网络配置模型。

## Requirements

### Requirement: stream socket 创建与 fd 集成

BigOS SHALL 提供用户可见的最小有界 TCP stream socket，表示为内核内部 `vfs::File` backend（独立于 UDP datagram socket 的 ops 表），并安装到既有 per-process fd 表。stream socket 创建 MUST 校验请求的 domain/type/protocol 属于 BigOS 有界 TCP 子集（IPv4 + `SOCK_STREAM` + TCP），成功时返回一个进程本地 fd，并复用既有 fd 分配、`dup`/`dup2`、`close` 与 `close-on-exec` 路径。该 stream socket fd MUST NOT 改变既有 syscall number、pipe、VFS、文件 fd 或既有 UDP datagram socket 的行为。

#### Scenario: 创建有界 stream socket

- **WHEN** 用户程序以 BigOS 有界 TCP 子集参数（IPv4 domain、`SOCK_STREAM`、TCP protocol）调用 socket 创建 syscall，且 fd 表有可用容量
- **THEN** BigOS MUST 创建一个 stream socket backend `vfs::File`，把它安装到最低可用 fd 并返回该 fd
- **AND** 该 fd MUST 可被既有 `close`、`dup`、`dup2`、`fcntl`（`close-on-exec`）路径处理

#### Scenario: 拒绝不支持的 stream socket 参数

- **WHEN** 用户程序请求超出 BigOS 有界 TCP 子集的 domain、type 或 protocol
- **THEN** BigOS MUST 以确定性负 errno 拒绝创建，且 MUST NOT 安装任何 fd

#### Scenario: 容量耗尽

- **WHEN** 进程 fd 表已达上限或协议层无可用连接资源
- **THEN** BigOS MUST 返回确定性负 errno（fd 表满与协议资源满使用各自确定性错误码）
- **AND** 失败路径 MUST NOT 泄漏部分创建的 `vfs::File` 或内核内部连接控制块

### Requirement: stream socket 主动打开（connect）

BigOS SHALL 提供 connect 操作，使一个 stream socket fd 通过内核内部 TCP 主动打开路径向目标 IPv4 地址与端口发起三次握手连接。connect MUST 校验 fd 是 stream socket、用户提供的定长地址结构与声明长度一致且地址族合法，并在 ordinary（可阻塞、非 IRQ）内核上下文推进。阻塞 fd 的 connect MUST 阻塞至连接进入 `ESTABLISHED` 或以确定性负 errno 失败；非阻塞 fd 的 connect 在握手未完成时 MUST 立即返回确定性进行中状态（`-EINPROGRESS`）。非阻塞 connect 的最终结果 MUST 可通过「`poll` 可写就绪 + `getsockopt(SOL_SOCKET, SO_ERROR)`」判定：`SO_ERROR` 读取并清除连接的 pending 错误码（0 表示成功，非 0 为确定性失败码），对齐 Linux/BSD 做法。对已连接或正在连接的 socket 重复 connect MUST 返回确定性错误（`-EISCONN`/`-EALREADY`）。

#### Scenario: 阻塞 connect 完成三次握手

- **WHEN** 用户程序对一个未连接的 stream socket fd 提供合法目的地址并调用阻塞 connect，且对端可完成握手
- **THEN** BigOS MUST 通过内核内部 TCP 主动打开完成三次握手进入 `ESTABLISHED` 并返回成功
- **AND** 该 socket 随后 MUST 能通过 `read`/`write` 进行有序字节流收发

#### Scenario: 非阻塞 connect 返回进行中并经 SO_ERROR 取结果

- **WHEN** 用户程序对一个非阻塞 stream socket fd 调用 connect 且握手尚未完成
- **THEN** BigOS MUST 立即返回 `-EINPROGRESS`，MUST NOT 阻塞
- **AND** 连接完成或失败后该 fd 的可写/错误就绪查询 MUST 反映之，且 `getsockopt(SOL_SOCKET, SO_ERROR)` MUST 返回确定性结果码（0 成功，或 `ECONNREFUSED`/`ECONNRESET` 等失败码）并清除 pending 错误

#### Scenario: getsockopt 仅支持 SO_ERROR

- **WHEN** 用户程序对 stream socket fd 调用 `getsockopt`，level/optname 不是 `SOL_SOCKET`/`SO_ERROR`
- **THEN** BigOS MUST 返回确定性 `-ENOPROTOOPT`，且 MUST NOT 声称支持该 option
- **AND** 对 `SOL_SOCKET`/`SO_ERROR` 的查询 MUST 通过校验后的用户缓冲写回结果码并清除连接 pending 错误

#### Scenario: 拒绝非法地址、非 stream fd 或重复连接

- **WHEN** connect 的 fd 不是 stream socket、地址结构长度与约定定长不一致、地址字段非法，或对已连接/正在连接的 socket 重复 connect
- **THEN** BigOS MUST 返回确定性负 errno（非 socket、非法地址、`-EISCONN`、`-EALREADY` 各自确定性），且 MUST NOT 破坏既有连接状态

#### Scenario: 连接被拒绝或复位

- **WHEN** connect 期间对端复位连接、握手重传超限或协议层以确定性状态放弃
- **THEN** BigOS MUST 以确定性负 errno（如 `-ECONNREFUSED`/`-ECONNRESET`）失败并回收连接资源
- **AND** MUST NOT 声称连接成功或泄漏连接控制块

### Requirement: stream socket 被动打开（listen 与 accept）

BigOS SHALL 提供 listen 与 accept 操作，使一个 stream socket fd 成为被动监听端并接受入站连接，采用 Linux/BSD 风格半连接(SYN)+全连接(accept)双队列模型。listen MUST 校验 fd 是已绑定本地端口的 stream socket，在协议层建立监听状态，并把 backlog 参数 clamp 到编译期定容的全连接队列上界。到达的 SYN MUST 派生独立子连接 TCB 登记到 listener 的半连接队列，完成三次握手后移入全连接队列。accept MUST 从 listener 的全连接队列取出一个 `ESTABLISHED` 连接，为其分配一个新的 stream socket fd 并返回；阻塞 fd 的 accept MUST 阻塞至有已完成连接可取，非阻塞 fd 的 accept 在无已完成连接时 MUST 立即返回 `-EAGAIN`。accept MUST 在 ordinary 内核上下文运行。当半连接或全连接队列容量耗尽时，新入站连接/新完成连接 MUST 以确定性状态处理，MUST NOT 无界扩张。

#### Scenario: listen 建立监听并 clamp backlog

- **WHEN** 用户程序对一个已绑定本地端口的 stream socket fd 调用 listen
- **THEN** BigOS MUST 在协议层建立监听状态，使该本地端口可接受入站三次握手
- **AND** backlog 参数 MUST 被 clamp 到编译期定容的全连接队列上界

#### Scenario: accept 取出已完成连接并分配新 fd

- **WHEN** 一个处于监听状态的 stream socket fd 调用 accept 且全连接队列中存在至少一个 `ESTABLISHED` 连接
- **THEN** BigOS MUST 取出一个已完成连接、为其分配一个新的 stream socket fd 并返回该 fd
- **AND** 新 fd MUST 可通过 `read`/`write` 进行有序字节流收发，且原 listener fd MUST 仍处于监听状态可继续 accept

#### Scenario: 非阻塞 accept 无连接返回 EAGAIN

- **WHEN** 一个非阻塞监听 stream socket fd 调用 accept 且全连接队列为空
- **THEN** BigOS MUST 立即返回 `-EAGAIN`，MUST NOT 阻塞或忙等

#### Scenario: accept 队列满的确定性处理

- **WHEN** listener 的全连接队列已满而新的入站连接完成握手，或半连接队列已满而新 SYN 到达
- **THEN** BigOS MUST 以确定性状态处理该超额连接（丢弃/复位）并递增确定性 TCP 丢弃诊断计数
- **AND** MUST NOT 无界扩张队列或覆盖已排队的连接

#### Scenario: 拒绝非 stream 或未监听 fd

- **WHEN** listen/accept 的 fd 不是 stream socket，或 accept 的 fd 未处于监听状态，或 listen 的 fd 未绑定本地端口
- **THEN** BigOS MUST 返回确定性负 errno，且 MUST NOT 改变该 fd 状态

### Requirement: stream socket 有序字节流 read/write/send 语义

BigOS SHALL 使 stream socket fd 的 `read`/`write`/`send` 通过内核内部 TCP 有序字节流路径收发数据。`read` MUST 从连接接收缓冲按序拷贝可用数据到用户缓冲并返回字节数；当对端已关闭（收到 FIN）且接收缓冲排空时 MUST 返回 0（EOF）。`write`/`send` MUST 把用户数据写入发送路径并返回已接受字节数（可能小于请求长度，受发送缓冲、通告窗口与重传队列的编译期上界约束）；`write` 等价于 `send(..., flags=0)`。对已关闭写方向的连接（收到 RST 或本地已发 FIN）`write`/`send` MUST 向当前进程投递 `SIGPIPE` 并返回 `-EPIPE`；当进程对 `SIGPIPE` 设置 `SIG_IGN` 或 `send` 携带 `MSG_NOSIGNAL` 时 MUST 仅返回 `-EPIPE` 而不投递终止性信号。read/write/send 长度 MUST 受既有 I/O 上限约束。对 stream socket 的 `sendto`/`recvfrom` MUST 返回确定性不支持错误。read/write/send MUST 在 ordinary 内核上下文运行，MUST NOT 从 IRQ 上下文操作连接缓冲。

#### Scenario: 有序字节流双向收发

- **WHEN** 两个已建立连接的 stream socket 一端 `write` 一段数据、另一端 `read`
- **THEN** BigOS MUST 使 `read` 按发送顺序返回数据字节（按用户缓冲长度有界截断，用户按返回值循环读取）
- **AND** `write` MUST 返回已接受字节数，MUST NOT 声称接受超过发送路径上界的数据

#### Scenario: 对端关闭后读到 EOF

- **WHEN** 连接对端已关闭写方向（其 FIN 已被有序消费）且本端接收缓冲已排空
- **THEN** BigOS 对该 stream socket fd 的 `read` MUST 返回 0（EOF）
- **AND** MUST NOT 无限阻塞或返回未定义数据

#### Scenario: write 到已关闭对端投递 SIGPIPE

- **WHEN** 用户程序对一个对端已关闭写方向（连接被 RST 或已进入不可写状态）的 stream socket fd 调用 `write`（或 `send` 不带 `MSG_NOSIGNAL`）
- **THEN** BigOS MUST 向当前进程投递 `SIGPIPE` 并返回 `-EPIPE`
- **AND** 当进程对 `SIGPIPE` 设置 `SIG_IGN` 时 MUST 仅返回 `-EPIPE` 而不终止进程

#### Scenario: send 带 MSG_NOSIGNAL 抑制 SIGPIPE

- **WHEN** 用户程序对一个对端已关闭写方向的 stream socket fd 调用 `send` 且 `flags` 含 `MSG_NOSIGNAL`，即使未忽略 `SIGPIPE`
- **THEN** BigOS MUST 仅返回 `-EPIPE`，MUST NOT 投递 `SIGPIPE`
- **AND** `send` 的数据路径 MUST 与 `write` 一致（`flags=0` 时行为等同 `write`）

#### Scenario: send 未知 flags 严格拒绝

- **WHEN** 用户程序调用 `send` 且 `flags` 含 `MSG_NOSIGNAL` 之外的任何非 0 位
- **THEN** BigOS MUST 返回确定性 `-EINVAL`，MUST NOT 发送任何数据
- **AND** MUST NOT 静默忽略未知位（为未来扩展 flag 语义预留干净边界）

#### Scenario: stream socket 的 sendto/recvfrom 不支持

- **WHEN** 用户程序对 stream socket fd 调用 `sendto` 或 `recvfrom`
- **THEN** BigOS MUST 返回确定性不支持错误（`-EOPNOTSUPP`），且 MUST NOT 进行任何 datagram 收发

#### Scenario: 未连接 socket 的 read/write

- **WHEN** 用户程序对一个尚未连接（未 connect/未 accept 得到）的 stream socket fd 调用 `read` 或 `write`
- **THEN** BigOS MUST 返回确定性负 errno（`-ENOTCONN`），且 MUST NOT 访问未初始化的连接缓冲

### Requirement: stream socket 就绪查询与多路复用集成

BigOS SHALL 为 stream socket fd 提供统一 fd 就绪查询（`poll_file`）与等待队列贡献（`poll_wait`），使其可被既有 `SYS_POLL` 多路复用与非阻塞 fd 路径统一处理。就绪判定 MUST 与 `read`/`write` 的 would-block 判定同源：`READY_READABLE` 当且仅当连接有按序数据可读、可读到 EOF、或（监听 fd）已完成连接队列非空；`READY_WRITABLE` 当且仅当连接已建立且发送路径有空间；`READY_ERROR` 当且仅当连接被复位或处于不可用状态。该判定 MUST 是 level-triggered。

#### Scenario: 可读就绪与非阻塞 read 一致

- **WHEN** stream socket 的就绪查询报告可读就绪
- **THEN** 随后对该 fd 的非阻塞 `read` MUST 立即成功（读到数据或 EOF），MUST NOT 返回 would-block

#### Scenario: 监听 fd 可 accept 就绪

- **WHEN** 一个监听 stream socket fd 的已完成连接队列非空
- **THEN** 就绪查询 MUST 对该 fd 报告可读就绪，使 `SYS_POLL` 可据此判定 accept 立即可成功
- **AND** 随后对该 fd 的非阻塞 accept MUST 立即返回一个新连接 fd

#### Scenario: 连接复位报告错误就绪

- **WHEN** 一个 stream socket 的连接被 RST 或重传超限复位
- **THEN** 就绪查询 MUST 对该 fd 报告错误就绪并在 `SYS_POLL` 中计入就绪
- **AND** 随后对该 fd 的 `read`/`write` MUST 返回确定性负 errno

#### Scenario: 多路复用阻塞唤醒

- **WHEN** 一个线程在 `SYS_POLL` 中因监听/连接 stream socket 无就绪而阻塞，随后有入站连接完成或数据到达
- **THEN** BigOS MUST 通过该 stream socket 贡献的等待队列唤醒该线程并重新扫描就绪集合
- **AND** MUST NOT 忙等轮转或向用户泄漏调度器私有等待常量

### Requirement: stream socket 非阻塞语义

BigOS SHALL 在 stream socket fd 被标记为非阻塞（open file description 粒度的既有非阻塞标志）时，使 `connect`/`accept`/`read`/`write` 在需要进入等待时返回确定性 would-block 而非阻塞：`connect` 握手未完成返回 `-EINPROGRESS`；`accept` 无已完成连接返回 `-EAGAIN`；`read` 无可读数据（且非 EOF）返回 `-EAGAIN`；`write` 发送路径无空间返回 `-EAGAIN`。该 would-block 判定 MUST 与就绪查询同源。已写出部分字节的 `write` MUST 返回已写字节数而非 would-block。

#### Scenario: 非阻塞 read 无数据返回 EAGAIN

- **WHEN** 一个非阻塞、已建立连接的 stream socket fd 在无按序可读数据且对端未关闭时被 `read`
- **THEN** BigOS MUST 返回 `-EAGAIN`，MUST NOT 在接收等待队列阻塞或忙等
- **AND** 对端写入数据后，对同一 fd 的非阻塞 `read` MUST 立即读到数据

#### Scenario: 非阻塞 write 无空间返回 EAGAIN

- **WHEN** 一个非阻塞 stream socket fd 在发送路径已满时被 `write`
- **THEN** 若尚未写出任何字节，BigOS MUST 返回 `-EAGAIN`，MUST NOT 在发送等待队列阻塞
- **AND** 若本次调用已写出部分字节后发送路径变满，BigOS MUST 返回已写出的字节数

#### Scenario: 阻塞语义在标志清除时保持

- **WHEN** stream socket fd 未设置非阻塞标志且当前操作需要等待
- **THEN** BigOS MUST 采用有界的 ordinary-context 等待队列阻塞语义，MUST NOT 因本能力提前返回 would-block

### Requirement: stream socket 生命周期与既有 fd 路径集成

BigOS SHALL 使 stream socket fd 在 `close`、`fork`、`close-on-exec` 与引用计数路径下行为确定。stream socket 的最后一个引用被释放时 MUST 通过既有 `vfs::release` 路径回收其内核内部连接控制块（发起或完成连接拆除），且 MUST NOT 重复释放或泄漏。`fork` MUST 通过既有引用计数共享同一 stream socket `vfs::File`/连接。accept 分配新 fd 的失败路径 MUST 回滚已取出的连接与新建的 `vfs::File`，不泄漏连接控制块或 fd。

#### Scenario: 关闭释放 stream socket 资源

- **WHEN** stream socket fd 的最后一个引用被 `close`（或进程退出统一回收）
- **THEN** BigOS MUST 通过既有 `vfs::release` 调用 stream socket backend 的 close，发起/完成连接拆除并回收连接控制块，且 MUST NOT 重复释放或泄漏

#### Scenario: fork 后共享 stream socket 引用

- **WHEN** 持有 stream socket fd 的进程 `fork`
- **THEN** 子进程 MUST 通过既有引用计数共享同一 stream socket `vfs::File`/连接
- **AND** 任一进程关闭其 fd MUST 只递减引用计数，仅在最后一次释放时回收连接控制块

#### Scenario: accept 失败路径不泄漏

- **WHEN** accept 已从队列取出一个已完成连接但随后 fd 表安装或 `vfs::File` 分配失败
- **THEN** BigOS MUST 回滚：回收新建的 `vfs::File` 并以确定性状态处理取出的连接，返回确定性负 errno
- **AND** MUST NOT 泄漏连接控制块或占用 fd 槽

### Requirement: stream socket syscall 与用户 libc 边界

BigOS SHALL 把 stream socket 的 `connect`/`listen`/`accept`、受限 `getsockopt` 与带 flags 的 `send` 暴露为一组从当前最高 syscall number 之后分配的新 syscall，并在内核与用户两份 syscall number 定义中保持一致；`socket` 创建复用既有 `SYS_SOCKET` 并放宽其参数校验以接受有界 TCP 子集。变更 MUST NOT 修改既有 syscall number 的取值或语义，MUST 在用户 libc 中提供最小 stream socket wrapper，保持 freestanding-safe 且使用既有 errno 翻译。用户可见地址结构 MUST 复用既有定长 `SockAddrIn` 布局。`send` MUST 提供 `flags` 参数承载 `MSG_NOSIGNAL`（数据路径等价于 `write`）；`write` 等价于 `send(..., flags=0)`。

#### Scenario: 新增 syscall number 双份一致

- **WHEN** 构建内核与用户态并运行既有 syscall number 源契约校验
- **THEN** 新增的 `connect`/`listen`/`accept`/`getsockopt`/`send` number MUST 在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 中相等
- **AND** 既有 syscall number 的取值与语义 MUST 保持不变

#### Scenario: 用户 libc stream socket wrapper 可用

- **WHEN** freestanding 用户程序链接本仓库有界 libc 并调用 `connect`/`listen`/`accept`/`getsockopt`/`send` wrapper
- **THEN** wrapper MUST 通过既有 `int 0x80` ABI 传参并把内核负返回翻译为 errno
- **AND** wrapper MUST NOT 依赖 hosted libc、异常、RTTI 或非 freestanding 运行时

### Requirement: stream socket 能力边界

BigOS SHALL 将本能力限定为最小有界 TCP stream socket 接口，MUST NOT 声称完整 POSIX socket 语义。具体地，本能力 MUST NOT 提供 `setsockopt`/`shutdown`/`getpeername`/`getsockname`/`sendmsg`/`recvmsg`/`accept4` 等调用矩阵、`AF_*`/`SOCK_*` 全量族、scatter-gather、ancillary data 或带外/urgent 数据；`getsockopt` MUST 仅支持 `SOL_SOCKET`/`SO_ERROR`（其它 level/optname 返回 `-ENOPROTOOPT`），`send` 的 `flags` MUST 仅识别 `MSG_NOSIGNAL`（不提供其它 `MSG_*` 矩阵），MUST NOT 提供 `SO_REUSEADDR` 等其它选项；MUST NOT 实现名字解析/DNS、通用 IP routing/转发或多接口地址模型；MUST NOT 引入超出 `bounded-tcp-path` 边界的 TCP 特性（拥塞控制、SACK、窗口缩放、时间戳/PAWS、keepalive）；除 broken-pipe 投递的 `SIGPIPE` 外 MUST NOT 引入新的信号语义。

#### Scenario: 不新增超范围 socket 接口

- **WHEN** stream socket 接口被编译进内核或验证被启用
- **THEN** 用户程序 MUST NOT 从本变更获得 `setsockopt`/`shutdown`/`accept4`/`sendmsg`/`recvmsg`、`SO_ERROR` 之外的 option 或 `MSG_NOSIGNAL` 之外的 `send` flag
- **AND** 既有 syscall number、fd 行为、UDP socket ABI 与 userland 程序 MUST 保持不变

#### Scenario: 不实现名字解析或完整 TCP 特性

- **WHEN** 连接建立或数据传输需要处理地址解析或特性协商
- **THEN** BigOS MUST 只提供基于 IPv4 地址的连接与 `bounded-tcp-path` 边界内的 TCP 行为
- **AND** MUST NOT 引入 DNS 名字解析或超范围 TCP 特性

### Requirement: stream socket 默认关闭验证与默认启动独立性

BigOS SHALL 通过默认关闭 smoke 验证 stream socket 接口，覆盖 `socket(SOCK_STREAM)` 创建与 fd 集成、`bind`/`listen`/`accept` 被动打开、`connect` 主动打开、`read`/`write` 有序双向数据交互与 EOF、就绪查询与非阻塞 would-block（`-EINPROGRESS`/`-EAGAIN`）、accept 队列满、连接复位（`-ECONNRESET`）与非法参数的确定性拒绝，并保证默认启动在未启用验证、无网络配置时不依赖 stream socket。验证 MUST 提供无需真实 tap/网卡即可运行的内核内部本机地址闭环路径（基于 loopback 就绪）；依赖不可用时 MUST 记录跳过原因与剩余风险。

#### Scenario: stream socket smoke 闭环通过

- **WHEN** 启用 stream socket 验证 build switch 并在受控环境（仅本机配置、无帧级设备的 loopback 就绪）运行 smoke
- **THEN** smoke MUST 覆盖 `socket`/`bind`/`listen`/`accept`/`connect`、有序双向 `read`/`write`、EOF、就绪与非阻塞 would-block（`-EINPROGRESS`/`-EAGAIN`）、非阻塞 `connect` 经 `getsockopt(SO_ERROR)` 取结果、`write` 到已关闭对端的 `SIGPIPE`+`-EPIPE`、accept 队列满、连接复位与非法参数错误路径，并发出确定性通过/失败 marker
- **AND** 成功 MUST 依赖协议层 TCP 状态机与 fd 集成，而非帧级设备 TX/RX

#### Scenario: 默认启动不依赖 stream socket

- **WHEN** stream socket 验证 switch 关闭或无网络配置
- **THEN** 默认启动、storage、filesystem、`/rw`、shell 与 userland baseline MUST 保持与 stream socket 能力无关并正常进入 shell
- **AND** 缺少 stream socket 初始化 MUST NOT 阻止正常启动验证运行

#### Scenario: 验证不可用时记录跳过

- **WHEN** QEMU、串口捕获或 x86_64-elf 工具链等验证依赖不可用
- **THEN** 验证记录 MUST 区分已通过、无法运行（含原因与剩余风险）与历史诊断
- **AND** MUST NOT 声称运行成功
