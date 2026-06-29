# 内核 fd 就绪模型

BigOS 把此前分散的描述符就绪判断（pipe 的 `read_ready`/`write_ready`、tty 的
`input_available`）收敛为单一的内核内 fd 就绪查询，并补齐 UDP socket 此前缺失的
接收等待队列。它是后续非阻塞描述符与多路复用 syscall 的内核内基础；本变更不引入
用户可见的多路复用 syscall，也不暗示完整 POSIX `poll`/`select`/`epoll` 语义。

## 统一查询入口

- `include/bigos/fs/vfs.h` 定义内核内就绪位标志 `READY_READABLE`、
  `READY_WRITABLE`、`READY_ERROR`。它们以按位或组合，是内核内部约定，不构成
  用户可见 syscall ABI。
- `FileOperations` 在既有 `read/close/write/lseek/truncate/readdir` 槽位之后
  追加一个可选 op `PollOp poll`。既有槽位不重排；`static_assert` 偏移守卫保证
  追加槽位始终在最后。
- `vfs::poll_file(File*)` 是唯一入口。`ops->poll` 非空时调用之；否则返回确定性
  默认 `READY_READABLE | READY_WRITABLE`（常规文件恒可读可写）。空文件返回
  `READY_ERROR`。
- `poll_file` 是纯只读快照：不出队数据、不阻塞调用方、不改变文件或后端打开状态。
  各后端的 `poll` 复用其阻塞读写路径所等待的同一谓词，因此“poll 报告可读”当且仅当
  阻塞读不会再阻塞。

## 各后端语义

- pipe（`kernel/core/ipc/pipe.cc`）：缓冲区有数据或写端已关闭（可读 EOF）时读端
  可读；缓冲区有空间或读端已关闭时写端可写，读端已关闭时写端置 `READY_ERROR`
  （broken pipe 倾向）。直接复用 `read_ready` 与 `write_ready`。
- socket（`kernel/core/net/socket.cc`）：已绑定且活跃、接收队列非空的 UDP 端点
  可读，可发送的端点可写，未绑定或失活的端点置 `READY_ERROR`。
- tty（`kernel/core/terminal/tty.cc`）：`TTY_OPS.poll` 复用 `input_available`
  （输入环记录或挂起的转义序列字节）作为可读位；终端写出方向恒可写，输入路径
  不置错误位。该 op 不出队任何 `TerminalInputRecord`，不改动输入环 head/tail。
  由于 fd 0/1/2 及任意 `dup` 副本共享同一 `TTY_OPS` 句柄，所有终端描述符就绪
  一致，且无需任何裸 fd 特例。
- 其它后端（exFAT、bigfs、file-mapping smoke）将 `poll` 留空，获得确定性的
  可读+可写默认值。

## socket 接收等待队列

- `UdpEndpoint`（`include/bigos/net.h`）新增 `sched::WaitQueue rx_wait`，在
  `udp_bind` 处初始化。
- 协议 RX 投递路径（`kernel/core/net/protocol.cc` 的 `handle_udp`）先把 datagram
  放入 `rx_queue`，随后调用 `sched::wake_all(&rx_wait)`。唤醒分配无关、可在
  IRQ/投递上下文安全调用，遵循既有唤醒约定（先入队再唤醒以闭合丢唤醒窗口）。
- `sys_recvfrom`（`kernel/core/syscall/syscall.cc`）对外行为保持不变：无数据且
  不可阻塞时仍返回 `-EWOULDBLOCK`（`NoData` 映射到 `-EAGAIN`）。本变更仅补齐
  等待队列与就绪查询，不改变返回码或既有的 poll-and-yield 契约。

## 验证

- 默认关闭的 xmake 开关 `--fd_readiness_smoke=y` 映射到 `BIGOS_FD_READINESS_SMOKE`
  宏，遵循既有 smoke 选项模式。默认关闭，不改变默认启动行为。
- `kernel/core/kernel.cc` 中的 smoke 入口构造 pipe、UDP socket、tty 三类描述符并
  断言：poll 的可读位与阻塞行为一致（“可读则阻塞读立即返回；不可读则无数据”）、
  poll 为不出队的只读快照、pipe 的可读 EOF 与 broken-pipe 错误位、socket 在空/
  非空/排空下结合 RX 唤醒的就绪、以及共享 `TTY_OPS` 的两个终端 fd 就绪一致。它
  在 COM1 输出确定性的 `BIGOS_FD_READINESS_PASSED` / `BIGOS_FD_READINESS_FAILED`
  标记，通过 QEMU headless 路径验证。

## 非目标

- 本变更不新增非阻塞读写描述符标志（`O_NONBLOCK`/`F_GETFL`/`F_SETFL`）。
- 不新增用户可见的多路复用 syscall（`poll`/`select` 类）；本变更不新增 syscall
  编号，不改动 syscall ABI。
- 就绪位仅为内核内部约定；用户可见的 POSIX 风格事件编码后续单独定义并在集中点
  转换。不声明完整 POSIX `poll`/`select`/`epoll` 语义，也不声明常规文件、块设备
  或其它描述符类型的“真实就绪”。
