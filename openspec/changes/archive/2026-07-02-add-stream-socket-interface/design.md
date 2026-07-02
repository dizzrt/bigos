## Context

M14 面向连接网络栈已完成两块基座：

- `bounded-tcp-path`（内核内部有界 TCP 协议路径）：`include/bigos/net/tcp.h` + `kernel/core/net/tcp.cc` 提供定容 `TcpControlBlock`（TCB）池与显式状态机、三次握手、RFC 6298 动态重传、有序交付与有界重组、标准 `2*MSL` `TIME_WAIT` 拆除，并暴露内核内部 API：`tcp_open`/`tcp_listen`/`tcp_send`/`tcp_receive`/`tcp_close`/`handle_tcp`/`tcp_pump`/`tcp_reset_state`。TCP 段经既有 `send_ipv4` 输出、经 `handle_ipv4` 输入分发，本机地址段复用 loopback 闭环。
- `loopback-network-path`：`send_ipv4` 对本机地址目的直接交 `handle_ipv4` 闭环，`State::LoopbackReady` 使内核内部网络在无帧级设备下可确定性、默认关闭地闭环验证。

用户可见 socket 侧已有 `minimal-socket-interface`（仅无连接 UDP）：`kernel/core/net/socket.cc` 把每个 UDP socket 表示为一个 `vfs::File`（`SOCKET_OPS` ops 表 + `private_data` 指向 `Socket{context, endpoint, local_port, bound}`），并已实现 `poll_file`/`poll_wait`（贡献 `endpoint->rx_wait`）。socket 系列 syscall 为 `SYS_SOCKET=55`/`SYS_BIND=56`/`SYS_SENDTO=57`/`SYS_RECVFROM=58`，`SYS_POLL=61` 提供有界多路复用（`bounded-fd-multiplexing`），`nonblocking-fd-io` 提供 OFD 粒度 `O_NONBLOCK`。当前最高 syscall 号为 `SYS_POLL=61`。

当前缺口：用户态无法进行任何面向连接通信。UDP socket 的 `read`/`write` 故意返回 `Unsupported`；没有 `connect`/`listen`/`accept`；TCP 连接未接入 fd/`poll`/非阻塞路径。

关键约束与可复用点：

- socket fd 对象模型（ops 表 + `private_data` + `is_*_file` ops 身份判定 + `vfs::release` 单次回收）已由 UDP socket 建立，stream socket 复用同一模式，新增第二套 `STREAM_SOCKET_OPS`。
- syscall 层已有用户缓冲校验/拷贝（`validate_user_buffer`/`validate_user_io_buffer`/`copy_current_user_buffer`/`copy_to_current_user_buffer`）、fd 安装（`install_fd_current`）、`can_block()` 守卫、`net_status_to_errno`/`vfs_status_to_syscall` 映射，stream socket 复用之。
- `SockAddrIn{family, port, addr}`（定长、host order、IPv4+port）已定义并被 UDP `bind`/`sendto`/`recvfrom` 使用，stream socket 的 `connect`/`accept` 地址复用同一结构。
- 协议路径的推进模型：RX/重传/`TIME_WAIT` 由 ordinary-context `tcp_pump`（沿用 ARP pending 的 tick 驱动）推进，MUST NOT 在 IRQ 上下文操作 TCB/缓冲。stream socket 的阻塞收发/连接/接受必须在 ordinary 上下文，通过 `tcp_pump` + 等待队列/`yield` 推进。
- **被动打开模型缺口**：现有 `handle_tcp`（`kernel/core/net/tcp.cc`）在 `LISTEN` TCB 收到 SYN 时，把 **listener TCB 本身**迁移为 `SynReceived`→`Established`（`listener->state = TcpState::SynReceived`）。这是单连接被动打开，无法支撑「一个 listen socket 反复 `accept` 多个连接」。要暴露 `listen`/`accept`，必须改为 listener 收 SYN 时派生子连接 TCB，listener 保持 `LISTEN`，并维护定容「已完成连接队列」。

本变更是这些内核 API 到 fd/syscall 的用户可见适配层，外加对 `bounded-tcp-path` 被动打开模型的必要修改。验证走默认关闭 smoke + 本机地址 loopback 闭环。

## Goals / Non-Goals

**Goals:**

- 新增 `SOCK_STREAM` socket 的 `vfs::File` backend，`private_data` 关联一个 TCP 连接句柄，接入既有 fd 表分配/`dup`/`fork`/`close`/`close-on-exec`/引用计数路径。
- 扩展 `SYS_SOCKET` 接受 `SOCKET_SOCK_STREAM` + `SOCKET_IPPROTO_TCP` 有界子集；新增 `SYS_CONNECT=62`/`SYS_LISTEN=63`/`SYS_ACCEPT=64`，双份 number（`include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h`）保持相等。
- stream socket fd 的 `read`/`write` 走 TCP 有序字节流（`tcp_receive`/`tcp_send`），`sendto`/`recvfrom` 对 stream socket 返回确定性不支持错误。
- 修改 `bounded-tcp-path` 被动打开：listener 保持 `LISTEN`，收 SYN 派生子连接 TCB，完成握手的连接进入 listener 名下定容已完成连接队列；新增内核内部「取一个已完成连接」入口；为 TCB 增加连接级等待队列供 fd readiness/阻塞使用。
- 接入 fd readiness/`SYS_POLL`：stream socket 的 `poll_file`/`poll_wait` 按连接状态报告可读/可写/错误并贡献等待队列。
- 定义有界阻塞与非阻塞语义：`connect`（阻塞至 `ESTABLISHED` 或失败；非阻塞返回 `-EINPROGRESS`）、`accept`（阻塞至有已完成连接；非阻塞无连接返回 `-EAGAIN`）、`read`/`write`（阻塞至可读/可写；非阻塞返回 `-EAGAIN`），均 ordinary-context、经 `tcp_pump` 推进、有界。
- 补充连接语义 errno；用户 libc 新增 `connect`/`listen`/`accept` wrapper 与 `SOCK_STREAM`/`IPPROTO_TCP` 常量。
- 新增默认关闭 `--stream_socket_smoke` 与确定性 COM1 marker，在 loopback 就绪下端到端验证 stream socket 生命周期与错误路径；默认启动不依赖 stream socket。

**Non-Goals:**

- 不实现完整 POSIX socket 语义：无 `AF_*`/`SOCK_*` 全量族、无 `getsockopt`/`setsockopt`/`shutdown`/`getpeername`/`getsockname`/`sendmsg`/`recvmsg`/`accept4`/`SO_REUSEADDR` 等选项/调用矩阵、无 scatter-gather/ancillary data、无带外/urgent 数据。
- 不实现完整 TCP 特性矩阵（沿用 `bounded-tcp-path` 边界：无拥塞控制、SACK、窗口缩放、时间戳/PAWS、keepalive）。
- 不实现名字解析/DNS（后续能力）；不实现通用 IP routing/转发或多接口地址模型。
- 不引入无界缓冲或用户可见 async I/O；不改动既有 UDP/ICMP/ARP 行为、帧级设备 TX/RX ownership 或 IRQ 边界。
- 不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局、CR3 切换或磁盘布局；不改动既有 syscall number 取值、UDP socket ABI、pipe/VFS/文件 fd 行为或默认用户态程序集合。

## Decisions

### 决策一：stream socket 作为第二套 `vfs::File` ops 表，`private_data` 关联 TCP 连接句柄

在 `kernel/core/net/socket.cc` 新增 `STREAM_SOCKET_OPS`（与既有 `SOCKET_OPS` 并列），`include/bigos/net/socket.h` 新增 `StreamSocket` 状态与 `stream_socket_create`/`is_stream_socket_file`/`stream_socket_state`。`StreamSocket` 至少持有：

```
struct StreamSocket {
    Context *context;            // owning protocol context (single default_context)
    TcpControlBlock *tcb;        // non-null once connect/accept established, or a LISTEN tcb
    uint16_t local_port;         // bound/listen local port
    enum { Unbound, Bound, Listening, Connecting, Connected, Closed } role;
};
```

- ops 表映射：`read` → `tcp_receive`（有序字节流，可读返回 0 表示对端已 FIN/EOF）；`write` → `tcp_send`（返回已接受字节数）；`sendto`/`recvfrom` 对 stream socket 返回 `EOPNOTSUPP`（datagram 专用路径）；`close` → `tcp_close` + 回收句柄；`poll`/`poll_wait` 见决策五；`lseek` → `NotSeekable`。
- ops 身份判定（`file->ops == &STREAM_SOCKET_OPS`）区分 stream 与 datagram socket，syscall 层据此路由 `connect`/`listen`/`accept` 只接受 stream socket、`bind`/`sendto`/`recvfrom` 各自校验类型。
- 复用既有 `vfs::release` 单次回收：`close` op 只在最后引用释放时回收，保证 close/fork/错误回滚路径无双重释放/泄漏。
- 备选（把 stream/datagram 合并进一套 ops + 运行期分支）被否：两者 read/write/poll 语义差异大（UDP 恒返回 `Unsupported`，TCP 是真实字节流），分开 ops 表更清晰且 ops 身份判定即类型判定。

### 决策二：syscall ABI —— 新增五个独立 syscall 号（62–66），`SYS_SOCKET` 放宽

从当前最高号 `SYS_POLL=61` 之后分配 `SYS_CONNECT=62`/`SYS_LISTEN=63`/`SYS_ACCEPT=64`/`SYS_GETSOCKOPT=65`/`SYS_SEND=66`，在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 同步新增并保持相等（既有 tests 源契约会校验一致）。`SYS_SOCKET` 扩展为接受 `SOCKET_SOCK_STREAM`+（`0`/`SOCKET_IPPROTO_TCP`）。

- 参数约定（沿用既有 socket syscall 风格，`SockAddrIn` 定长 host-order）：
  - `SYS_CONNECT(fd, const SockAddrIn* addr, addrlen)` → 0 / `-EINPROGRESS`（非阻塞进行中）/ 负 errno。
  - `SYS_LISTEN(fd, backlog)` → 0 / 负 errno；`backlog` clamp 到编译期 `STREAM_ACCEPT_QUEUE_CAPACITY`。
  - `SYS_ACCEPT(fd, SockAddrIn* peer_out, uint32_t* addrlen_io)` → 新连接 fd / 负 errno；`peer_out` 为空时不回写地址。
  - `SYS_GETSOCKOPT(fd, level, optname, void* optval, uint32_t* optlen_io)` → 0 / 负 errno。本变更**仅**实现 `level=SOL_SOCKET, optname=SO_ERROR` 一种组合（读取并清除连接的 pending 错误码，供非阻塞 `connect` 完成判定，对齐 Linux/BSD 做法）；其它 level/optname 一律返回 `-ENOPROTOOPT`。这是一个受限的 `getsockopt`，不引入完整 option 矩阵、不实现 `setsockopt`。
  - `SYS_SEND(fd, const void* buf, len, flags)` → 已发送字节数 / 负 errno。用于承载 `MSG_NOSIGNAL`：数据路径与 stream socket `write` 一致（`tcp_send`），但当 `flags & MSG_NOSIGNAL` 时对 broken-pipe **只返回 `-EPIPE`、抑制 `SIGPIPE`**。`flags` 仅识别 `MSG_NOSIGNAL`；含任何未知非 0 位 MUST 返回 `-EINVAL`（严格校验，为未来扩展 flag 语义预留）。stream socket 的 `write` 等价于 `send(..., flags=0)`。
- 选择独立号（而非单个多路复用 `socketcall`）：与既有 55–58/61 一致的一号一调用风格，libc wrapper 直连、可读性高、tests 源契约逐号校验更直接；`accept` 需返回新 fd，独立号语义最自然。
- 引入 `SYS_GETSOCKOPT(SO_ERROR)` 的原因：用户选择对齐行业标准（Linux/BSD）的非阻塞 `connect` 三段式完成判定（`connect`→`-EINPROGRESS`；`poll(POLLOUT)`；`getsockopt(SO_ERROR)` 取最终结果）。「可写就绪」无法区分「连接成功」与「连上又被 RST」，两者 fd 都变可写，只有 `SO_ERROR` 能返回真实结果，因此 `SO_ERROR` 是标准做法的必要拼图。范围严格限定为单一 option，保持有界。
- 引入 `SYS_SEND(MSG_NOSIGNAL)` 的原因：用户选择实现 `MSG_NOSIGNAL`。`MSG_NOSIGNAL` 是 `send(2)` 的**每调用 flag**，而既有 `SYS_SENDTO` 无 flags 参数、stream socket `write` 走 flagless 的 `vfs::write` op，都无法承载它；故新增带 flags 的 `SYS_SEND`。数据路径复用 `tcp_send`，仅 broken-pipe 时按 flag 决定是否投递 `SIGPIPE`。`flags` 采用严格校验：含未知非 0 位返回 `-EINVAL`（不静默忽略），为未来扩展 `MSG_*` 语义预留干净的兼容边界。
- 备选（扩展 `fcntl`/`poll` 承载连接语义）被否：`connect`/`listen`/`accept`（尤其 `accept` 返回新 fd）无法自然映射到 `fcntl`/`poll`，会引入畸形 ABI。用户已确认采用独立号方案。
- 备选（不引入 `getsockopt`、用「二次 `connect` 取错误码」替代 `SO_ERROR`）被否：用户选择对齐 Linux/BSD 标准三段式，故实现受限 `SO_ERROR` 而非重入式 `connect` 变通。
- 备选（`MSG_NOSIGNAL` 复用 `SYS_SENDTO` 的 addrlen 位或加 `fcntl` 标志）被否：`SYS_SENDTO` 无 flags 语义位、`fcntl` 是 fd 粒度而非每调用粒度，均无法表达 `send(2)` 的每调用 `MSG_NOSIGNAL`；新增带 flags 的 `SYS_SEND` 最自洽。
- `SYS_SOCKET` 参数放宽只加分支，不改既有 `SOCK_DGRAM`+UDP 取值与语义。

### 决策三：修改 `bounded-tcp-path` 被动打开为 Linux/BSD 风格「listener + 半连接队列(SYN queue) + 全连接队列(accept queue)」

这是本变更唯一的协议层修改，其余为纯适配。现有 `handle_tcp` 把 listener TCB 本身迁移为连接，只能支撑单连接被动打开。用户选择对齐 Linux/BSD 的双队列被动打开模型：

- `LISTEN` TCB 保持 `LISTEN` 不被复用为连接。listener 持有两条定容队列（对齐 Linux 的 SYN queue 与 accept queue）：
  - **半连接队列（SYN queue）**：收到匹配 SYN 时从定容 TCB 池派生一个**子连接 TCB**（四元组绑定该 peer），置 `SynReceived`、回 SYN,ACK，并登记到半连接队列（容量 `STREAM_SYN_QUEUE_CAPACITY`）。子 TCB 记录其 listener（`listener_local_port` 或 listener 指针）。
  - **全连接队列（accept queue）**：子连接收到最终 ACK 迁移到 `Established` 后，从半连接队列移入全连接队列（容量 `STREAM_ACCEPT_QUEUE_CAPACITY`，即 `backlog` clamp 上界），等待 `accept` 取走。
- 队列满的确定性处理（对齐 Linux 语义边界）：半连接队列满时对新 SYN 确定性丢弃（不回 SYN,ACK），全连接队列满时握手完成的连接确定性丢弃/复位，均递增 `tcp_dropped`；不无界扩张、不复用 listener 自身作为连接。
- 新增内核内部入口（`include/bigos/net/tcp.h`），例如：
  - `Status tcp_accept(Context*, TcpControlBlock* listener, TcpControlBlock** out)`：从 listener 的全连接队列取一个 `Established` 子连接；无则返回 `NoData`。
  - TCB 增加连接级等待队列 `sched::WaitQueue`（数据到达 / 连接进入 `Established` / 可 accept / 状态错误时唤醒），供 fd readiness 与阻塞路径登记。listener 用其等待队列表达「全连接队列非空（有新连接可 accept）」。
- 对既有主动打开（`tcp_open`/`SynSent`→`Established`）、数据/重传/拆除/`TIME_WAIT`/诊断/容量语义**不变**。
- backlog 与连接池的关系：半连接与全连接的子 TCB 都从同一定容 TCB 池分配；两条队列容量为独立编译期上界。见决策九的容量不变式。
- 备选（不分半/全连接队列、只用单一已完成队列）被否：用户选择对齐 Linux/BSD 双队列，显式区分「握手中」与「握手完成待 accept」两个阶段，使队列满语义与 `poll` 可 accept 判定与 Linux 一致。
- 备选（在 socket 层之外用影子表模拟子连接）被否：连接四元组匹配、序号推进、重传都在 TCB 内，子连接必须是真实 TCB；在协议层内建子连接是唯一自洽方案。

### 决策四：stream socket 的 `read`/`write` 有序字节流语义

- `read(fd, buf, len)`：经 `tcp_receive` 从连接接收缓冲拷贝按序数据到用户缓冲；有数据返回字节数；对端已 FIN 且缓冲排空返回 0（EOF）；`ESTABLISHED` 但暂无数据时，阻塞 fd 经 `tcp_pump`+等待队列等待，非阻塞 fd 返回 `-EAGAIN`；连接被 RST/复位返回 `-ECONNRESET`；未连接返回 `-ENOTCONN`。
- `write(fd, buf, len)`：经 `tcp_send` 把用户数据写入发送缓冲并按 MSS 有界分段发送；返回已接受字节数（可能小于 `len`，受发送缓冲/通告窗口/重传队列上界约束）；发送缓冲满时阻塞 fd 等待可写、非阻塞 fd 返回 `-EAGAIN`；对端已关闭（连接进入不可写状态，收到 RST 或本地已发 FIN）写入时，对齐行业标准做法：**向当前进程投递 `SIGPIPE` 并返回 `-EPIPE`**（除非该 socket 设置了 `MSG_NOSIGNAL` 语义或进程忽略 `SIGPIPE`，则仅返回 `-EPIPE`）；未连接返回 `-ENOTCONN`。见决策十。
- 用户缓冲长度受既有 `SYS_IO_MAX_LEN=512` 与 TCP 发送缓冲上界约束；单次 `read`/`write` 有界，不保证一次传输全部字节（字节流语义，用户按返回值循环）。
- `sendto`/`recvfrom` 对 stream socket 返回 `-EOPNOTSUPP`。

### 决策五：fd readiness / `SYS_POLL` / 非阻塞集成，判定与阻塞谓词同源

stream socket 实现 `poll_file`/`poll_wait`，与 `read`/`write` 的 would-block 判定同源（复用同一「可读/可写/错误」谓词），满足 `bounded-fd-multiplexing` 与 `nonblocking-fd-io` 的一致性要求：

- `poll_file` 就绪位：
  - `READY_READABLE`：连接接收缓冲有按序数据可读，或对端已 FIN（可读到 EOF），或（listener）已完成连接队列非空（`accept` 可立即成功）。
  - `READY_WRITABLE`：连接处于 `Established` 且发送缓冲/窗口有空间。
  - `READY_ERROR`：连接被 RST/重传超限复位、或处于不可用状态。
- `poll_wait`：贡献该连接（或 listener）的连接级等待队列句柄；无有效连接/队列时返回 0（多路复用视其 `poll_file` 已 `READY_ERROR` 而立即就绪，不阻塞）。
- 非阻塞语义：`connect` 非阻塞在 `SynSent` 未完成时返回 `-EINPROGRESS`（后续用 `poll` 可写就绪判定完成；重复 `connect` 在进行中返回 `-EALREADY`，已连接返回 `-EISCONN`）；`accept`/`read` 非阻塞无就绪返回 `-EAGAIN`；`write` 非阻塞不可写返回 `-EAGAIN`。
- level-triggered、就绪与非阻塞读写一致，均复用同一谓词，避免判定漂移。

### 决策六：有界阻塞经 ordinary-context `tcp_pump` + 等待队列推进

阻塞 fd 的 `connect`/`accept`/`read`/`write` 必须在 ordinary（`can_block()`）上下文，沿用既有 socket recvfrom 的「有界推进 + 等待/让出」但改为等待队列驱动：

- 推进：在等待前后调用 `tcp_pump(context)` 让协议状态机推进（本机 loopback 下段立即闭环），并在连接级等待队列上阻塞，由段到达/连接完成/状态变化唤醒；设置有界上限（最大等待轮次或超时）避免无界占用。
- IRQ 边界：所有 TCP 操作 ordinary-context；syscall 入口先 `can_block()` 守卫（不可阻塞返回 `-EWOULDBLOCK`）。MUST NOT 从 IRQ 分配/阻塞/操作 TCB 缓冲。
- 备选（socket 层自建定时器/回调驱动）被否：违反既有 ordinary-context 推进边界，重复协议层机制。

### 决策七：errno 与 libc wrapper

- `include/bigos/errno.h` append-only 新增连接语义错误码：`ECONNREFUSED`(111)、`ECONNRESET`(104)、`EISCONN`(106)、`ENOTCONN`(107)、`EINPROGRESS`(115)、`EALREADY`(114)、`ENOPROTOOPT`(92)（`getsockopt` 未支持 option 用；取值对齐 Linux x86_64），既有错误码不变。
- `user/libc` 新增 `connect`/`listen`/`accept`/`getsockopt` wrapper（经 `int 0x80` 传参、负返回翻译 errno）与 `SOCKET_SOCK_STREAM`/`SOCKET_IPPROTO_TCP`/`SOL_SOCKET`/`SO_ERROR` 常量镜像，`user/libc/include/sys/socket.h` 与内核 `SockAddrIn` 布局保持一致，freestanding-safe。

### 决策九：容量常量与不变式（避免 loopback 下 TCB 池饥饿）

本机地址闭环下 client 与 server 共用同一定容 TCB 池。一条经 `connect`/`accept` 建立的 loopback 连接同时占用 3 个 TCB：listener（保持 `LISTEN`）+ client 主动打开 TCB + server 子连接 TCB。现有 `TCP_CONNECTION_CAPACITY=4` 在保持 listener 的前提下只能容纳 1 条已建立 loopback 连接，且 `TIME_WAIT`（此处 `2*MSL=300ms`）拆除后仍占槽，反复建连会立即打满。

- 决定：把 `TCP_CONNECTION_CAPACITY` 从 4 上调到 **8**（仍编译期定容，TCB 约 6KB/个 → 池约 48KB 静态，可接受）；`STREAM_SYN_QUEUE_CAPACITY = 2`、`STREAM_ACCEPT_QUEUE_CAPACITY = 2`，`listen` 的 `backlog` clamp 到 `STREAM_ACCEPT_QUEUE_CAPACITY`。
- 不变式（`static_assert` 固化，对齐 lwIP 的分池思路）：`TCP_CONNECTION_CAPACITY >= 1(listener) + STREAM_SYN_QUEUE_CAPACITY + STREAM_ACCEPT_QUEUE_CAPACITY + 主动连接预算`，保证 listener + 半连接 + 全连接 + client 侧连接可同时存在而不误报 `TableFull`。
- 队列容量为编译期上界；上调 `TCP_CONNECTION_CAPACITY` 不改变协议语义，仅放宽并发连接数，`static_assert` 守护 `TCP_MSS`/缓冲等既有关系不变。

### 决策十：broken-pipe 统一投递 `SIGPIPE` + 返回 `-EPIPE`，`MSG_NOSIGNAL` 抑制

用户选择对齐行业标准做法：对已关闭写方向的连接 `write`/`send`，默认向当前进程投递 `SIGPIPE` 并返回 `-EPIPE`。BigOS 既有信号集合（`include/bigos/signal.h`）**没有 `SIGPIPE`**，故本变更在信号子系统 append-only 引入它：

- `SIGPIPE = 13`（对齐 POSIX/Linux 编号），默认动作 Terminate；纳入既有每进程 pending 位图/掩码/处置表（`SIG_MAX` 当前为 31，13 在范围内，位图无需扩宽）。可被用户 `sigaction` 忽略/捕获，被忽略时写操作仅返回 `-EPIPE`（对齐 `SIG_IGN` 行为）。
- 投递复用既有信号投递路径（在 ordinary/syscall 返回用户态边界置 pending 位，不在 IRQ 上下文投递，不在内核态运行 handler）。
- **统一 pipe 与 stream socket broken-pipe 语义（用户选定）**：既有 `kernel/core/ipc/pipe.cc` 读端全关的写路径当前仅返回 `-EPIPE`（`pipe-ipc` 规格把 `SIGPIPE` 列为可选 MAY）。本变更把它改为**必需**：pipe 写者在读端全关时除 `-EPIPE` 外 MUST 一并投递 `SIGPIPE`，与 stream socket 一致。这修改既有 `pipe-ipc` 可观察行为（返回码不变、新增信号投递），需同步更新 `pipe-ipc`/`signals` 规格与 `--pipe_smoke`/`--signal_smoke` 断言，并在验证记录区分「本变更引入」与「既有 pipe 行为变化」。
- **抽出统一 broken-pipe 投递辅助（用户选定）**：pipe 与 stream socket 的「返回 `-EPIPE` + 按 `SIG_IGN`/`MSG_NOSIGNAL` 决定是否投递 `SIGPIPE`」逻辑 MUST 收敛到一个内核内部辅助（例如 `bigos::signal::raise_broken_pipe(process, suppress)` 或等价入口），两条路径统一调用。这保证两路径的返回码、信号投递与抑制条件行为一致、只有一处判定，避免语义漂移，并让 `--pipe_smoke`/`--signal_smoke`/`--stream_socket_smoke` 断言同源。
- **`MSG_NOSIGNAL`（用户选定实现）**：新增 `SYS_SEND(fd, buf, len, flags)` 承载 `MSG_NOSIGNAL`（见决策二）。`flags & MSG_NOSIGNAL` 置位时 broken-pipe 只返回 `-EPIPE`、抑制 `SIGPIPE`；`write`/`sendto` 等价 `flags=0`；`flags` 含未知非 0 位返回 `-EINVAL`（严格校验）。抑制 `SIGPIPE` 的两条途径：每调用 `MSG_NOSIGNAL`，或进程 `SIG_IGN` SIGPIPE。
- 备选（只返回 `-EPIPE`、不引入 `SIGPIPE`）被否：用户选择对齐行业标准，故引入 `SIGPIPE`。
- 备选（只对 stream socket 投 SIGPIPE、不动 pipe）被否：用户选择统一 pipe/stream socket broken-pipe 语义。

### 决策八：验证以本机地址内核内部闭环 + 用户态端到端为主

新增默认关闭 `--stream_socket_smoke`（映射 `BIGOS_STREAM_SOCKET_SMOKE` → `BIGOS_STREAM_SOCKET_PASSED/FAILED`），在 `LoopbackReady`（仅本机配置、无帧级设备）下覆盖：`socket(SOCK_STREAM)` 创建与 fd 集成、`bind`/`listen`/`accept` 被动打开、`connect` 主动打开完成握手、`read`/`write` 有序双向数据交互（含短写/循环）、EOF（对端 FIN）、`poll` 就绪报告与非阻塞 would-block（`-EINPROGRESS`/`-EAGAIN`）、accept 队列满、连接被 RST/复位（`-ECONNRESET`）、非法参数/非 stream fd 的确定性拒绝、以及默认启动不依赖 stream socket。通过 QEMU headless 观测确定性 marker；默认（开关关闭）构建不含 stream socket smoke 线程，默认启动行为不变。

## Risks / Trade-offs

- [修改协议层被动打开引入回归] 改 `handle_tcp` 的 LISTEN 路径为派生子连接会触及最复杂的 TCP 状态机 → 缓解：只改 LISTEN→SYN,ACK 派生这一处，`SynReceived`/`Established`/数据/拆除路径复用现有逻辑；保留 `bounded-tcp-path` 原 smoke（`--tcp_path_smoke`）作为回归基线，另加 stream socket smoke 覆盖多连接 accept；GCC 交叉构建 + QEMU headless 双 marker。
- [子连接 TCB 与已完成连接队列耗尽定容池] 一个 listener 大量半开/已完成连接会占满定容 TCB 池与 accept 队列 → 缓解：子连接从同一定容 TCB 池分配、accept 队列独立编译期上界；两处满以确定性 `TableFull`/丢弃处理并递增 `tcp_dropped`，不无界扩张；backlog 参数 clamp 到队列上界。
- [阻塞收发/连接/接受的无界占用] 阻塞路径若无界等待或忙等会独占 CPU → 缓解：复用连接级等待队列真正阻塞让出（非忙等），`tcp_pump` 推进，设有界上限；`can_block()` 守卫拒绝 IRQ/不可阻塞上下文。
- [IRQ 上下文误用] 从 IRQ 触发 TCP 收发/推进/唤醒会违反边界 → 缓解：所有 stream socket 操作 ordinary-context，syscall 入口 `can_block()` 守卫，沿用 `bounded-tcp-path` 的 ordinary-context 推进模型。
- [readiness 判定与 read/write would-block 漂移] 若 poll 与非阻塞读写各自维护判定会不一致 → 缓解：`poll_file` 与 `read`/`write` would-block 复用同一「可读/可写/错误」谓词（同源），满足 `bounded-fd-multiplexing`/`nonblocking-fd-io` 的一致性 scenario。
- [socket 生命周期跨 fork/close/accept 的引用与回收] stream socket 与其 TCB 的生命周期需在 close/fork/错误回滚下确定 → 缓解：复用 `vfs::release` 单次回收与 `vfs::retain` 引用计数，`close` op 回收 TCB（发起/完成 `tcp_close`）；`accept` 失败路径回滚已取出的子连接与新 `vfs::File`，不泄漏 TCB 或 fd。
- [新增 syscall 号与双份契约漂移] 62–66 若两份头不一致会破坏用户态 → 缓解：`include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 同步新增，既有 tests 源契约逐号校验相等；不改既有号取值。
- [默认启动误启用协议栈] 新增路径若被默认启动触达会影响 boot → 缓解：stream socket 仅在用户显式 syscall 或 smoke 下驱动；协议 `default_context` 默认 `Disabled`；smoke 默认关闭；默认启动回归纳入验证。
- [errno 取值与 Linux 对齐] 新增连接 errno 取值须与既有约定一致 → 缓解：append-only、对齐 Linux x86_64 数值、既有取值不变，源级契约/检索确认无重复定义（`unified-errno` 单一来源）。
- [loopback 下 TCB 池饥饿] 现 `TCP_CONNECTION_CAPACITY=4` 在保持 listener 时 loopback 只能 1 条连接，`TIME_WAIT` 占槽加剧 → 缓解：上调到 8 并用 `static_assert` 固化容量不变式（决策九）；smoke 覆盖多连接 accept 与 `TIME_WAIT` 回收后重建连接。
- [引入 SIGPIPE 且修改既有 pipe broken-pipe 行为] 新增 `SIGPIPE=13` 扩展信号集合，且把 pipe 读端全关写路径的 SIGPIPE 从可选改为必需会改变既有 pipe 可观察行为 → 缓解：`SIGPIPE` append-only 加入固定信号集合（`SIG_MAX=31` 无需扩宽位图），默认 Terminate、可 `SIG_IGN`；pipe/stream socket 共用同一 broken-pipe 投递辅助；`--pipe_smoke`/`--signal_smoke` 断言同步更新，验证记录区分「本变更引入」与「既有 pipe 行为变化」；返回码仍为 `-EPIPE`，仅新增信号投递。
- [MSG_NOSIGNAL 需要带 flags 的 send 入口] 既有 `SYS_SENDTO`/`write` 无 flags，无法承载每调用 `MSG_NOSIGNAL` → 缓解：新增 `SYS_SEND(fd, buf, len, flags)`，`flags` 仅识别 `MSG_NOSIGNAL`，`write`/`sendto` 等价 `flags=0`；含未知非 0 flags 位严格返回 `-EINVAL`（不静默忽略），为未来 `MSG_*` 扩展预留干净边界。
- [受限 getsockopt 被误用为完整 option 矩阵] 仅实现 `SO_ERROR` 的 `getsockopt` 可能被期望支持更多 option → 缓解：其它 level/optname 一律返回 `-ENOPROTOOPT`，spec 与注释明确本变更只支持 `SOL_SOCKET/SO_ERROR`，不实现 `setsockopt`。

## Migration Plan

- 增量为主 + 协议层与 IPC 层各一处修改：
  1. `bounded-tcp-path`：修改 `kernel/core/net/tcp.cc` 的 `handle_tcp` LISTEN 路径为 Linux/BSD 双队列（半连接 SYN queue + 全连接 accept queue）派生子连接 TCB 模型；上调 `TCP_CONNECTION_CAPACITY` 到 8 并新增队列容量常量；在 `include/bigos/net/tcp.h` 新增 `tcp_accept` 入口、TCB 连接级等待队列与 listener 双队列字段（append-only，`static_assert` 守护既有布局假设与容量不变式）。
  2. socket 层：`include/bigos/net/socket.h` + `kernel/core/net/socket.cc` 新增 `StreamSocket`/`STREAM_SOCKET_OPS`/`stream_socket_create`/`is_stream_socket_file`/`stream_socket_state` 与 poll ops。
  3. syscall 层：`include/bigos/syscall.h` 新增 `SYS_CONNECT`/`SYS_LISTEN`/`SYS_ACCEPT`/`SYS_GETSOCKOPT`/`SYS_SEND` 与 stream socket 常量（`SOCKET_SOCK_STREAM`/`SOCKET_IPPROTO_TCP`/`SOL_SOCKET`/`SO_ERROR`/`MSG_NOSIGNAL`）；`kernel/core/syscall/syscall.cc` 新增 `sys_connect`/`sys_listen`/`sys_accept`/`sys_getsockopt`/`sys_send`、`SYS_SOCKET` stream 分支、stream socket 的 `read`/`write` 路由与 dispatch case。
  4. errno：`include/bigos/errno.h` append-only 新增连接错误码（含 `ENOPROTOOPT`）。
  5. 信号：`include/bigos/signal.h` append-only 新增 `SIGPIPE=13`（默认 Terminate、可 `SIG_IGN`），并新增统一 broken-pipe 投递辅助（例如 `raise_broken_pipe(process, suppress)`）；stream socket `write`/`send` 到已关闭对端经该辅助投递 SIGPIPE + 返回 `-EPIPE`（`MSG_NOSIGNAL`/`SIG_IGN` 抑制）。
  6. IPC 管道：`kernel/core/ipc/pipe.cc` 读端全关写路径除返回 `-EPIPE` 外调用同一 broken-pipe 投递辅助投递 `SIGPIPE`（与 stream socket 一处判定）；同步更新 `pipe-ipc`/`signals` 规格与 `--pipe_smoke`/`--signal_smoke` 断言。
  7. 用户态：`user/libc/include/sys_nr.h` 同步 number；`user/libc/include/errno.h` 与 `signal` 常量镜像 `SIGPIPE`；新增 `connect`/`listen`/`accept`/`getsockopt`/`send` wrapper 与 `sys/socket.h` 常量（含 `MSG_NOSIGNAL`）。
  8. 验证：`xmake.lua` 新增 `--stream_socket_smoke` 开关映射 `BIGOS_STREAM_SOCKET_SMOKE`；smoke 入口 `#ifdef` 守卫，`kernel.cc` 守卫 spawn。
- ABI 迁移：不改既有 syscall number 取值/语义、UDP socket ABI、pipe/VFS/文件 fd 返回码；pipe 读端全关写路径新增 `SIGPIPE` 投递为可观察行为增强（返回码仍 `-EPIPE`）。
- 回滚策略：stream socket 为独立 ops 表 + 独立 syscall 号 + smoke 默认关闭，可整体回退；协议层被动打开修改可回退到「listener 自身迁移」的原行为（但会失去 accept 能力）；pipe SIGPIPE 投递可单独回退到仅 `-EPIPE`。
- 构建/静态检查：GCC 交叉构建默认与 smoke 两配置、clang/clangd 辅助静态检查、tests 源级契约（syscall number 双份一致、errno 单一来源、`SockAddrIn` 布局镜像、signal 常量镜像）、QEMU headless stream socket smoke marker + 默认启动回归 marker + 保留 `--tcp_path_smoke`/`--pipe_smoke`/`--signal_smoke` 回归。

## Open Questions

（以下先前问题已确认）

- 已完成连接队列容量与 TCB 池占用：已确认对齐 Linux/BSD 双队列模型（半连接 SYN queue + 全连接 accept queue），并把 `TCP_CONNECTION_CAPACITY` 上调到 8、`STREAM_SYN_QUEUE_CAPACITY=2`、`STREAM_ACCEPT_QUEUE_CAPACITY=2`，`backlog` clamp 到全连接队列上界，`static_assert` 固化容量不变式（含 loopback 下 listener+client+child 同池占用）。见决策三与决策九。
- 非阻塞 `connect` 完成判定：已确认对齐 Linux/BSD 标准三段式（`connect`→`-EINPROGRESS`；`poll(POLLOUT)`；`getsockopt(SO_ERROR)` 取最终结果），为此引入受限 `SYS_GETSOCKOPT`（仅 `SOL_SOCKET/SO_ERROR`，其它返回 `-ENOPROTOOPT`），不实现 `setsockopt`。见决策二。
- broken-pipe 语义：已确认对齐行业标准，引入 `SIGPIPE=13`（默认 Terminate、可 `SIG_IGN`），stream socket `write`/`send` 到已关闭写方向投递 SIGPIPE + 返回 `-EPIPE`。见决策十。
- pipe 统一 SIGPIPE：已确认把既有 pipe 读端全关写路径的可选 `SIGPIPE` 改为必需，与 stream socket 统一；同步更新 `pipe-ipc`/`signals` 规格与 `--pipe_smoke`/`--signal_smoke`。见决策十与迁移步骤 6。
- `MSG_NOSIGNAL`：已确认实现，新增带 flags 的 `SYS_SEND=66` 承载 `MSG_NOSIGNAL`，置位时 broken-pipe 只返回 `-EPIPE`、抑制 `SIGPIPE`。见决策二与决策十。
- `SYS_SEND` 的 `flags` 未知位处理：已确认采用严格校验，含未知非 0 位返回 `-EINVAL`（不静默忽略），为未来扩展 flag 语义预留干净边界。见决策二与决策十。
- broken-pipe 投递辅助：已确认抽出统一内核辅助，pipe 与 stream socket 共用同一「返回 `-EPIPE` + 按 `SIG_IGN`/`MSG_NOSIGNAL` 决定是否投递 `SIGPIPE`」逻辑，保证两路径行为与验证一致。见决策十与迁移步骤 5/6。

（无剩余未决问题。）
