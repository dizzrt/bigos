## Why

内核内部有界 TCP 协议路径（`bigos::net::tcp_*`：定容 TCB 池 + 状态机、三次握手、RFC 6298 重传、有序交付与重组、标准 `2*MSL` `TIME_WAIT` 拆除）已经落地，但它只暴露内核内部 API，用户态程序仍无法进行任何面向连接的通信。既有用户可见 socket 只覆盖无连接 UDP datagram（`SYS_SOCKET`/`SYS_BIND`/`SYS_SENDTO`/`SYS_RECVFROM`），没有 `connect`/`listen`/`accept`，也没有把 TCP 连接接入 fd、非阻塞与 `SYS_POLL` 多路复用路径。

本变更在既有 fd/syscall/多路复用路径之上提供最小有界 stream socket 接口，让用户程序能创建 `SOCK_STREAM` socket、主动 `connect` 到本机地址、被动 `listen`/`accept`，并用 `read`/`write`/`poll` 进行可靠有序字节流交互，同时明确不声称完整 POSIX socket 语义。这是 M14 面向连接网络栈用户可见目标的收口，也是后续名字解析（DNS client）等能力的前置。

## What Changes

- 扩展内核内部 socket 对象层：新增 `SOCK_STREAM` socket 的 `vfs::File` backend（与既有 UDP datagram backend 并列的第二套 ops 表），通过 `File::private_data` 持有对内核 `bigos::net::TcpControlBlock` 的引用/句柄，并接入既有 fd 表分配/复制/关闭/`close-on-exec`/`fork` 路径。stream socket fd 的 `read`/`write` 走 TCP 有序字节流（`tcp_receive`/`tcp_send`），不再返回 UDP backend 的 `Unsupported`。
- 新增最小有界 stream socket 系列 syscall，从当前最高号 `SYS_POLL = 61` 之后分配：`SYS_CONNECT = 62`、`SYS_LISTEN = 63`、`SYS_ACCEPT = 64`、`SYS_GETSOCKOPT = 65`（受限：仅 `SOL_SOCKET`/`SO_ERROR`，用于对齐 Linux/BSD 非阻塞 `connect` 完成判定）、`SYS_SEND = 66`（`fd, buf, len, flags`，承载 `MSG_NOSIGNAL` 抑制信号）。number 必须在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 同步新增并保持相等（既有 tests 源契约会校验一致）。`socket`/`bind`/`close`/`read`/`write`/`dup`/`dup2`/`fcntl` 复用既有路径；`SYS_SOCKET` 扩展为接受 `SOCKET_SOCK_STREAM` + `SOCKET_IPPROTO_TCP`。
- 接入 fd readiness 与多路复用：为 stream socket 提供 `poll_file`（可读=接收缓冲有序数据可读或连接已就绪/半关闭，可写=`ESTABLISHED` 且发送侧有空间，错误=RST/重传超限复位）与 `poll_wait`（贡献 TCP 连接的等待队列），使 stream socket fd 能被既有 `SYS_POLL` 与非阻塞 fd 路径统一处理。
- 定义有界被动打开语义（对齐 Linux/BSD 双队列）：`listen` 在协议层建立 `LISTEN` TCB；到达的 SYN MUST 派生一个**子连接 TCB**（`SYN_RECEIVED`）登记到 listener 的半连接队列（SYN queue）而 listener 保持 `LISTEN`，完成三次握手（`ESTABLISHED`）的子连接移入 listener 的全连接队列（accept queue）；`accept` 从全连接队列取出一个连接并为其分配一个新 stream socket fd。两条队列为编译期定容上界，队列满时对新 SYN/新完成连接以确定性状态处理。这需要修改既有 `bounded-tcp-path` 的被动打开行为（原实现把 listener TCB 本身迁移为连接，只支持单连接），并上调 `TCP_CONNECTION_CAPACITY`（现为 4）以容纳 loopback 下 listener + client + child 同池占用。
- 定义有界阻塞与非阻塞语义：`connect`/`accept`/`read`/`write` 在阻塞 fd 上复用既有 ordinary-context 等待队列阻塞并由 `tcp_pump` 推进；在非阻塞 fd 上返回确定性 would-block（`connect` 进行中返回 `-EINPROGRESS`、`accept`/`read` 无就绪返回 `-EAGAIN`），非阻塞 `connect` 完成后经 `poll(POLLOUT)` + `getsockopt(SO_ERROR)` 取最终结果。
- 在用户 libc 中新增最小 stream socket wrapper（`connect`/`listen`/`accept`/`getsockopt`/`send` 与 `SOCK_STREAM`/`IPPROTO_TCP`/`SOL_SOCKET`/`SO_ERROR`/`MSG_NOSIGNAL` 常量）及对应头，保持 freestanding-safe。
- 补充连接语义所需 errno（如 `ECONNREFUSED`/`ECONNRESET`/`EISCONN`/`ENOTCONN`/`EINPROGRESS`/`EALREADY`/`ENOPROTOOPT`），统一在 `include/bigos/errno.h` 单一来源新增。
- 引入 `SIGPIPE`（对齐行业标准）：既有信号集合无 `SIGPIPE`，本变更 append-only 新增 `SIGPIPE = 13`（默认 Terminate、可 `SIG_IGN`）；stream socket `write`/`send` 到已关闭写方向的连接时投递 `SIGPIPE` 并返回 `-EPIPE`。统一 broken-pipe 语义：既有 pipe 读端全关的写路径（当前仅返回 `-EPIPE`）MUST 一并投递 `SIGPIPE`，使 pipe 与 stream socket 行为一致（这修改既有 `pipe-ipc` 的可选 `SIGPIPE` 为必需）。新增 `send(fd, buf, len, flags)` 承载 `MSG_NOSIGNAL`：置位时抑制 `SIGPIPE` 只返回 `-EPIPE`（进程 `SIG_IGN` 亦可抑制）。
- 新增默认关闭的 stream socket 验证 smoke 与确定性 COM1 标记：在 loopback 就绪（无 tap/网卡）环境下覆盖 `socket(SOCK_STREAM)`→`bind`→`listen`→`accept` 与 `connect`、`read`/`write` 有序双向交互、`poll` 就绪报告、非阻塞 would-block、accept 队列满/连接被复位等错误路径，以及默认启动不依赖 stream socket。

## Capabilities

### New Capabilities

- `stream-socket-interface`: 定义 BigOS 用户可见的最小有界 TCP stream socket 接口，覆盖 `SOCK_STREAM` socket fd 对象模型、`connect`/`listen`/`accept` 系列 syscall 边界、Linux/BSD 风格半连接(SYN)+全连接(accept)双队列、stream socket 的 `read`/`write` 有序字节流语义、受限 `getsockopt(SO_ERROR)` 供非阻塞 `connect` 完成判定、`write` 到已关闭对端的 `SIGPIPE`+`-EPIPE` 语义、与 fd readiness/`SYS_POLL` 多路复用及非阻塞 fd 路径的集成、有界阻塞/would-block 语义、与既有 fd 表/`fork`/`close-on-exec` 的集成、错误与诊断语义，以及默认关闭验证边界。该能力保持有界：不声称完整 POSIX socket 语义（无 `AF_*`/`SOCK_*` 全量族、无 `setsockopt`/`shutdown`/`getpeername`/`getsockname`/`accept4` 矩阵、`getsockopt` 仅支持 `SOL_SOCKET/SO_ERROR`、无 scatter-gather/ancillary data、无 `SO_REUSEADDR` 等其它选项、无名字解析）。

### Modified Capabilities

- `bounded-tcp-path`: 修改被动打开模型为 Linux/BSD 双队列以支持 `accept`：`LISTEN` TCB 收到 SYN 时 MUST 派生一个独立的子连接 TCB（`SYN_RECEIVED`）登记到 listener 的半连接队列（SYN queue）并保持 listener 处于 `LISTEN`（原实现把 listener TCB 本身迁移为 `SYN_RECEIVED`→`ESTABLISHED`，只支持单连接）；完成三次握手的子连接 MUST 移入 listener 的全连接队列（accept queue）供上层取用。新增/扩展内核内部入口用于「从 listener 取一个已完成连接」，并为 TCB 提供供 fd readiness 使用的连接级等待队列（数据可读/连接可 accept/连接就绪唤醒）。上调 `TCP_CONNECTION_CAPACITY`（现为 4）以容纳 loopback 下 listener+client+child 同池占用，`static_assert` 固化容量不变式。既有主动打开、数据传输、重传、拆除、`TIME_WAIT`、诊断计数与协议语义保持不变。
- `minimal-socket-interface`: 扩展 `SYS_SOCKET` 以接受 `SOCKET_SOCK_STREAM` + `SOCKET_IPPROTO_TCP` 的有界子集（既有 `SOCK_DGRAM`+UDP 语义保持不变），并明确 stream socket fd 的 `read`/`write`/`poll` 行为不同于 datagram socket 的 `Unsupported` 读写与 `sendto`/`recvfrom` 数据路径。
- `bounded-fd-multiplexing`: 扩展 `SYS_POLL` 覆盖的描述符类型，新增 stream socket fd 的就绪定义（可读/可写/错误）与等待队列贡献，使 stream socket 参与既有定容多路复用；`SYS_POLL` 的 ABI、容量上界（`POLL_MAX_FDS`）、超时与就绪模型语义不变。
- `nonblocking-fd-io`: 扩展非阻塞 fd 行为覆盖 stream socket 的 `connect`/`accept`/`read`/`write`，在非阻塞标志置位时返回确定性 would-block（`-EINPROGRESS`/`-EAGAIN`）而非阻塞；既有 `O_NONBLOCK`/`fcntl` 语义与 pipe/tty/UDP socket 行为不变。
- `unified-errno`: 以 append-only 方式新增面向连接语义所需的 POSIX 错误码常量（如 `ECONNREFUSED`/`ECONNRESET`/`EISCONN`/`ENOTCONN`/`EINPROGRESS`/`EALREADY`/`ENOPROTOOPT`），既有错误码取值与语义不变。
- `signals`: 以 append-only 方式向固定信号集合新增 `SIGPIPE = 13`（默认动作 Terminate、可 `sigaction` 忽略/捕获），纳入既有每进程 pending 位图/掩码/处置表与既有信号投递路径；`SIG_MAX` 当前为 31，无需扩宽位图。既有信号编号、默认动作与投递边界保持不变。
- `pipe-ipc`: 把既有「读端全关写返回 `-EPIPE`」的可选 `SIGPIPE` 增强改为必需——写者除返回 `-EPIPE` 外 MUST 一并投递 `SIGPIPE`，与 stream socket broken-pipe 语义统一；既有 pipe 创建、阻塞读写、EOF、`dup`/`dup2`、生命周期语义不变。

## Impact

- 受影响内核子系统：syscall 层（`kernel/core/syscall` 新增 `sys_connect`/`sys_listen`/`sys_accept`/`sys_getsockopt`/`sys_send` 与 `SYS_SOCKET` stream 分支）、进程/fd 层（`kernel/core/proc` 复用 fd 安装/复制、多路复用 `poll_fds_current`、非阻塞标志）、socket backend（`kernel/core/net/socket.cc` 新增 stream socket ops 表与 TCB 句柄适配，`include/bigos/net/socket.h` 追加 stream socket 声明）、内核内部 TCP 层（复用 `bigos::net::tcp_open`/`tcp_listen`/`tcp_send`/`tcp_receive`/`tcp_close`/`tcp_pump`；在 `include/bigos/net/tcp.h` 追加 `tcp_accept` 取连接、连接级等待队列与 listener 半/全连接双队列字段，并上调 `TCP_CONNECTION_CAPACITY`）、信号子系统（`include/bigos/signal.h` 新增 `SIGPIPE`）、IPC 管道层（`kernel/core/ipc/pipe.cc` 读端全关写路径追加投递 `SIGPIPE`）、公开内核头（`include/bigos/syscall.h`、`include/bigos/errno.h`）、用户 libc（`user/libc`）与默认关闭 smoke 入口。
- 依赖既有能力：`bounded-tcp-path`（内核内部 TCP 状态机与 `tcp_*` API）、`loopback-network-path`（本机地址闭环，使验证无需 tap/网卡）、`minimal-socket-interface`（socket fd 对象模型与用户态地址/缓冲拷贝契约、`SockAddrIn`）、`fd-readiness-model` 与 `bounded-fd-multiplexing`（`poll_file`/`poll_wait` 与 `SYS_POLL`）、`nonblocking-fd-io`（`O_NONBLOCK` 与 would-block）、`kernel-blocking-primitives`（等待队列/ordinary-context 阻塞）、`signals`（每进程 pending/掩码/处置表与投递路径，承载 `SIGPIPE`）。stream socket 层是这些内核 API 到 fd/syscall 的适配层，不改变协议层的有界容量边界或需求。
- ABI/接口影响：新增 syscall number（62/63/64/65/66）必须同步更新 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 并保持相等；`SYS_SOCKET` 参数校验放宽以接受 stream 子集，不改变既有 number 取值或既有 datagram 语义；新增 `SIGPIPE=13` 为 append-only、不改既有信号编号；既有 pipe 读端全关写路径追加 `SIGPIPE` 投递属可观察行为增强（返回码仍为 `-EPIPE`）；不改动 `int 0x80` 入口、寄存器传参顺序或既有 fd/VFS/pipe/UDP socket ABI。
- 受协议层现状约束：TCP TCB 池、发送/接收缓冲、重传队列、重组槽均为编译期定容；`accept` 半/全连接队列/backlog 亦为定容上界，队列满以确定性状态处理；RX/重传/`TIME_WAIT` 推进复用既有 ordinary-context `tcp_pump` 轮询，MUST NOT 在 IRQ 上下文操作 TCB 或缓冲；本变更需在 socket 层显式定义连接建立/接受/收发的有界阻塞与 would-block 语义，而非声称通用 POSIX 阻塞。
- 不改动启动地址、链接地址、页表布局、磁盘布局、IDT/syscall vector、CR3 切换；不改动默认用户态程序集合或默认启动依赖，默认启动在无网络后端时仍正常进入 shell。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 交叉工具链与 xmake；QEMU headless 为主要 smoke 路径；stream socket 内核闭环基于本机地址 loopback，不依赖 tap 权限或真实/仿真网卡；连接数、backlog、缓冲与队列容量均为编译期定容上界；内存使用通过显式内核分配路径或有界静态/栈缓冲；辅助 Python 验证通过 `uv run ...`。
- 非目标：不实现完整 POSIX socket 语义——无 `AF_*`/`SOCK_*` 全量族、无 `setsockopt`/`shutdown`/`getpeername`/`getsockname`/`sendmsg`/`recvmsg`/`accept4` 等调用矩阵、`getsockopt` 仅支持 `SOL_SOCKET/SO_ERROR`（其它返回 `-ENOPROTOOPT`）、无 `SO_REUSEADDR` 等其它选项、无 scatter-gather/ancillary data、无带外/urgent 数据；不实现完整 TCP 特性矩阵（沿用 `bounded-tcp-path` 边界：无拥塞控制、SACK、窗口缩放、时间戳/PAWS、keepalive）；不实现名字解析/DNS（留待后续能力）；不实现通用 IP routing/转发或多接口地址模型；不引入无界缓冲或用户可见 async I/O；不引入 `SIGPIPE` 之外的新信号或作业控制、不实现 `MSG_NOSIGNAL` 之外的信号选项矩阵；不改动既有 UDP/ICMP/ARP 行为、帧级设备 TX/RX ownership 或 IRQ 边界；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
