## 1. 协议层被动打开模型修改（bounded-tcp-path，Linux/BSD 双队列）

- [x] 1.1 在 `include/bigos/net.h`/`include/bigos/net/tcp.h` 上调 `TCP_CONNECTION_CAPACITY`（现为 4）到 8，新增 `STREAM_SYN_QUEUE_CAPACITY=2`、`STREAM_ACCEPT_QUEUE_CAPACITY=2`，并以 `static_assert` 固化容量不变式：`TCP_CONNECTION_CAPACITY >= 1(listener) + STREAM_SYN_QUEUE_CAPACITY + STREAM_ACCEPT_QUEUE_CAPACITY + 主动连接预算`，同时守护 `TCP_MSS`/缓冲等既有关系不变。
- [x] 1.2 在 `include/bigos/net/tcp.h` 为 `TcpControlBlock` 新增 listener/子连接关联字段（listener 标识/指针、是否为已接受连接）、连接级 `sched::WaitQueue`（数据到达/连接建立/错误唤醒），并为 listener 新增半连接队列（SYN queue）与全连接队列（accept queue）表示（记录子连接槽索引）；append-only 追加，`static_assert` 守护既有布局假设不被破坏。
- [x] 1.3 在 `kernel/core/net/tcp.cc` 的 `handle_tcp` LISTEN 路径改为：listener 保持 `Listen`，收到匹配 SYN 时从定容 TCB 池派生子连接 TCB（绑定 peer 四元组、置 `SynReceived`、回 SYN,ACK）登记半连接队列；TCB 池无空闲槽或半连接队列满时确定性丢弃/复位并递增 `tcp_dropped`。
- [x] 1.4 子连接收到最终 ACK 进入 `Established` 时，从半连接队列移入 listener 的全连接队列并唤醒 listener 连接级等待队列；全连接队列满时确定性丢弃/复位并递增 `tcp_dropped`；保持既有 `SynReceived`/`Established`/数据/拆除路径逻辑复用。
- [x] 1.5 新增内核内部入口 `tcp_accept(Context*, TcpControlBlock* listener, TcpControlBlock** out)`：从全连接队列取一个 `Established` 子连接（空则返回 `NoData`）；在数据到达/连接就绪/复位处补齐对连接级等待队列的唤醒调用。
- [x] 1.6 复核 `tcp_reset_state`、TCB 回收（`recycle_tcb`）与 `tcp_pump` 对新增子连接/半连接/全连接队列/等待队列的处理，确保回收不泄漏、不遗留悬挂队列项，且推进仍为 ordinary-context。
- [x] 1.7 更新（不破坏）`--tcp_path_smoke` 下 `tcp.cc` 内既有被动打开 smoke 断言以适配「listener 保持 LISTEN + 双队列子连接」模型，作为协议层回归基线。

## 2. errno 扩展（unified-errno）

- [x] 2.1 在 `include/bigos/errno.h` 以 append-only 方式新增 `ECONNREFUSED=111`、`ECONNRESET=104`、`EISCONN=106`、`ENOTCONN=107`、`EINPROGRESS=115`、`EALREADY=114`、`ENOPROTOOPT=92`（对齐 Linux x86_64），既有取值不变。
- [x] 2.2 检索确认无子系统重复定义这些错误码，且新增均为 freestanding-safe 编译期常量；`user/libc/include/errno.h` 同步镜像。

## 3. 信号扩展与 broken-pipe 统一（signals / pipe-ipc）

- [x] 3.1 在 `include/bigos/signal.h` append-only 新增 `SIGPIPE=13`（默认动作 Terminate），确认 `SIG_MAX=31` 无需扩宽位图，纳入既有 pending/掩码/处置表；`user/libc` 侧 signal 常量镜像 `SIGPIPE`。
- [x] 3.2 在信号默认动作表/查询路径把 `SIGPIPE` 归入 Terminate（可 `SIG_IGN`/捕获），复用既有返回用户态边界投递路径，不在 IRQ 上下文投递。
- [x] 3.3 抽出统一 broken-pipe 投递辅助（例如 `bigos::signal::raise_broken_pipe(process, suppress)`，返回 `-EPIPE` + 按 `SIG_IGN`/`MSG_NOSIGNAL` 在一处判定是否投递 `SIGPIPE`），供 pipe 与 stream socket 共用，保证两路径行为与验证同源。
- [x] 3.4 修改 `kernel/core/ipc/pipe.cc` 读端全关写路径：除返回 `-EPIPE` 外调用统一辅助投递 `SIGPIPE`（`SIG_IGN` 时仅 `-EPIPE`）；同步更新 `--pipe_smoke`/`--signal_smoke` 断言，验证记录区分本变更引入与既有 pipe 行为变化。

## 4. stream socket backend（stream-socket-interface / minimal-socket-interface）

- [x] 4.1 在 `include/bigos/net/socket.h` 新增 `StreamSocket` 状态结构（context、tcb、local_port、role 枚举）与声明 `stream_socket_create`/`is_stream_socket_file`/`stream_socket_state`，以及默认关闭 smoke 入口声明。
- [x] 4.2 在 `kernel/core/net/socket.cc` 新增 `STREAM_SOCKET_OPS`：`read`→`tcp_receive`（EOF 返回 0）、`write`→`tcp_send`（返回已接受字节；对端已关闭写方向经统一 broken-pipe 辅助投递 `SIGPIPE`+返回 `-EPIPE`，`SIG_IGN`/`MSG_NOSIGNAL` 时仅 `-EPIPE`）、`sendto`/`recvfrom` 走不支持（由 syscall 层对 stream fd 返回 `-EOPNOTSUPP`）、`close`→`tcp_close`+回收、`lseek`→NotSeekable。
- [x] 4.3 实现 stream socket 的 `poll_file`（可读=有序数据/EOF/listener 全连接队列非空；可写=Established 且发送有空间；错误=复位）与 `poll_wait`（贡献连接级/ listener 等待队列），判定与 read/write would-block 同源。
- [x] 4.4 实现 `stream_socket_create`（分配 `Socket`+`vfs::File`、设置 ops、readable/writable、失败回滚）与 ops 身份判定辅助函数。

## 5. syscall 层（stream-socket-interface / minimal-socket-interface）

- [x] 5.1 在 `include/bigos/syscall.h` 新增 `SYS_CONNECT=62`/`SYS_LISTEN=63`/`SYS_ACCEPT=64`/`SYS_GETSOCKOPT=65`/`SYS_SEND=66` 及注释，新增 `SOCKET_SOCK_STREAM`/`SOCKET_IPPROTO_TCP`/`SOL_SOCKET`/`SO_ERROR`/`MSG_NOSIGNAL` 常量；不改既有号取值。
- [x] 5.2 扩展 `sys_socket`：接受 `SOCK_STREAM`+（0/`IPPROTO_TCP`）分支，走 `stream_socket_create`，安装 fd 并失败回滚；既有 `SOCK_DGRAM`+UDP 分支不变。
- [x] 5.3 实现 `sys_connect(fd, SockAddrIn*, addrlen)`：校验 stream fd/地址/长度；`can_block()` 守卫；调用 `tcp_open` 主动打开；阻塞 fd 经 `tcp_pump`+等待队列等待至 `Established`/失败，非阻塞 fd 未完成返回 `-EINPROGRESS`；重复连接返回 `-EISCONN`/`-EALREADY`；失败映射 `-ECONNREFUSED`/`-ECONNRESET` 并记录连接 pending 错误供 `SO_ERROR` 读取。
- [x] 5.4 实现 `sys_listen(fd, backlog)`：校验已绑定 stream fd；调用 `tcp_listen` 建监听；backlog clamp 到 `STREAM_ACCEPT_QUEUE_CAPACITY`。
- [x] 5.5 实现 `sys_accept(fd, SockAddrIn* peer_out, uint32_t* addrlen_io)`：`can_block()` 守卫；经 `tcp_accept` 取全连接队列连接；阻塞 fd 无连接时经等待队列等待、非阻塞返回 `-EAGAIN`；为取出连接创建新 stream socket `vfs::File`、安装新 fd、回写 peer 地址；失败路径回滚 `vfs::File` 与取出的连接不泄漏。
- [x] 5.6 实现 `sys_getsockopt(fd, level, optname, optval, optlen_io)`：仅支持 `SOL_SOCKET`/`SO_ERROR`（校验用户缓冲、写回并清除连接 pending 错误码），其它 level/optname 返回 `-ENOPROTOOPT`。
- [x] 5.7 实现 `sys_send(fd, buf, len, flags)`：校验 stream fd/缓冲；数据路径复用 `tcp_send`；`flags` 仅识别 `MSG_NOSIGNAL`（broken-pipe 时经统一辅助抑制 `SIGPIPE` 只返回 `-EPIPE`），含未知非 0 flags 位严格返回 `-EINVAL`（不静默忽略）。
- [x] 5.8 在 `dispatch` 中路由 stream socket fd 的 `read`/`write` 到 `STREAM_SOCKET_OPS`（经既有 `vfs::read`/`vfs::write` ops 分发），对 stream fd 的 `sendto`/`recvfrom` 返回 `-EOPNOTSUPP`；新增 `SYS_CONNECT`/`SYS_LISTEN`/`SYS_ACCEPT`/`SYS_GETSOCKOPT`/`SYS_SEND` case。

## 6. 用户 libc（stream-socket-interface）

- [x] 6.1 在 `user/libc/include/sys_nr.h` 同步新增 `SYS_CONNECT 62`/`SYS_LISTEN 63`/`SYS_ACCEPT 64`/`SYS_GETSOCKOPT 65`/`SYS_SEND 66`，与内核头保持相等。
- [x] 6.2 在 `user/libc/include/sys/socket.h` 镜像新增 `SOCK_STREAM`/`IPPROTO_TCP`/`SOL_SOCKET`/`SO_ERROR`/`MSG_NOSIGNAL` 常量（与内核一致），确认 `SockAddrIn` 布局镜像一致。
- [x] 6.3 在 `user/libc` 新增 `connect`/`listen`/`accept`/`getsockopt`/`send` wrapper（`int 0x80` 传参、负返回翻译 errno），freestanding-safe，无 hosted 运行时依赖。

## 7. 验证开关与 smoke（stream-socket-interface）

- [x] 7.1 在 `xmake.lua` 新增默认关闭 `--stream_socket_smoke`，映射 `BIGOS_STREAM_SOCKET_SMOKE`（依赖 `BIGOS_USER_PROCESS` 等既有用户态开关按既有模式组织）。
- [x] 7.2 实现 `#ifdef BIGOS_STREAM_SOCKET_SMOKE` 下的 smoke 入口（内核内部/用户态端到端，loopback 就绪）：覆盖 `socket(SOCK_STREAM)`→`bind`→`listen`→`accept` 与 `connect`、`read`/`write`/`send` 有序双向交互与短写循环、EOF、`poll` 就绪、非阻塞 `-EINPROGRESS`/`-EAGAIN`、非阻塞 `connect`+`getsockopt(SO_ERROR)` 取结果、`write` 到已关闭对端 `SIGPIPE`+`-EPIPE`、`send(MSG_NOSIGNAL)` 抑制 `SIGPIPE`、`send` 未知 flags 返回 `-EINVAL`、accept 队列满、连接复位 `-ECONNRESET`、非 stream/未连接/非法参数拒绝；发出 `BIGOS_STREAM_SOCKET_PASSED/FAILED` marker。
- [x] 7.3 在 `kernel/core/kernel.cc` 以 `#ifdef` 守卫 spawn smoke 线程；确认默认（开关关闭）构建不含 smoke 线程且默认启动行为不变。

## 8. 静态检查与构建验证

- [x] 8.1 运行 `xmake`（x86_64-elf-gcc 默认配置）确认内核构建通过；修复本变更引入的编译错误与合理新警告。需先确认 `x86_64-elf-gcc`/`x86_64-elf-g++`/`x86_64-elf-ld` 与 xmake 可用，否则记录阻塞。
- [x] 8.2 运行 `xmake f --stream_socket_smoke=y && xmake` 确认 smoke 配置构建通过；确认上调 `TCP_CONNECTION_CAPACITY` 后 `--tcp_path_smoke` 与默认配置均构建通过。
- [x] 8.3 clang/clangd 辅助静态检查（freestanding C++17、x86_64 target、项目 include 路径、无异常/RTTI/hosted 运行时）覆盖新增/修改的 `kernel/core/net/socket.cc`、`kernel/core/net/tcp.cc`、`kernel/core/ipc/pipe.cc`、`include/bigos/net/*.h`、`include/bigos/signal.h`、`kernel/core/syscall/syscall.cc`；修复本变更引入的诊断，区分历史诊断/本变更诊断/工具链 false positive。若 clang/clangd 不可用则记录原因与剩余风险。

## 9. 运行期 smoke 与回归验证

- [x] 9.1 QEMU headless 运行 stream socket smoke（`uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/... --expect-serial-marker BIGOS_STREAM_SOCKET_PASSED`），确认通过 marker；`uv` 不可用则记录阻塞。
- [x] 9.2 QEMU headless 运行默认启动回归（`xmake run qemu -- --display none`）确认默认启动进入 shell（`BIGOS_USER_EXEC`）不依赖 stream socket。
- [x] 9.3 运行 `--tcp_path_smoke` 回归确认协议层双队列被动打开修改与容量上调未破坏既有 TCP smoke（`BIGOS_TCP_PATH_PASSED`）；运行 `--pipe_smoke`/`--signal_smoke` 回归确认 pipe broken-pipe 统一投递 `SIGPIPE` 后既有管道/信号行为符合更新后的断言（`BIGOS_PIPE_PASSED`/`BIGOS_SIGNAL_PASSED`）。
- [x] 9.4 运行既有 tests 源级契约（syscall number 双份一致、errno 单一来源、`SockAddrIn` 布局镜像、signal 常量镜像含 `SIGPIPE`），确认新增号/常量一致；通过 `uv run pytest` 执行相关契约测试，记录结果或不可用原因。

## 10. 文档与验证记录

- [x] 10.1 按需同步 `docs/en`（canonical）与 `docs/zh` 中的网络/socket/信号/管道能力描述，保持有界语义（不声称完整 POSIX socket），使用仓库相对路径。
- [x] 10.2 在变更目录补充 validation 记录：区分已通过、无法运行（含原因与剩余风险）、历史诊断与本变更诊断；显式记录既有 pipe broken-pipe 行为变化（新增 `SIGPIPE` 投递）。
- [x] 10.3 归档时把本变更的 delta specs 提升合并入主规格（`stream-socket-interface` 新增，`bounded-tcp-path`/`minimal-socket-interface`/`bounded-fd-multiplexing`/`nonblocking-fd-io`/`unified-errno`/`signals`/`pipe-ipc` 更新）。
