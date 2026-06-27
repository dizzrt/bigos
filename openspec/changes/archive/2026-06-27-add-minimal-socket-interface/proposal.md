## Why

BigOS 已经具备 frame-level virtio-net 设备接口和内核内部有界网络协议路径（以太网/ARP/IPv4/ICMP/UDP），但这些 UDP datagram 收发能力只暴露在内核内部 `bigos::net` API，用户态程序仍无法进行任何网络通信。
本变更面向 M11.3，在既有 fd/syscall 路径之上提供最小用户可见 socket 接口，让用户程序能通过 socket 进行基础 UDP 通信，同时明确不声称完整 POSIX socket 语义或完整网络栈。

## What Changes

- 新增内核内部 socket 对象层：把每个 UDP socket 表示为一个 `vfs::File` backend（沿用现有 pipe fd 的 ops-table 多态模式），通过 `File::private_data` 持有对内核 `bigos::net::UdpEndpoint` 的引用，并接入既有 fd 表分配/复制/关闭/`close-on-exec`/`fork` 路径。
- 新增最小有界 socket 系列 syscall，从当前最高号 `SYS_UTIMENS = 54` 之后的 `55` 起分配，覆盖 `socket`、`bind`、`sendto`、`recvfrom`、`close`（复用既有 `SYS_CLOSE`）；socket fd 也可通过既有 `read`/`write`/`dup`/`dup2`/`fcntl` 路径被关闭与复制。number 必须在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 同步新增并保持相等。
- 新增有界用户态地址结构（`bigos` 风格的 `sockaddr`-lite，仅 IPv4 + port）与用户缓冲拷贝边界，复用既有 `validate_user_buffer`/`copy_from/to_user` 与 `SYS_IO_MAX_LEN = 512` 上限，与 `UDP_MAX_PAYLOAD = 512` 对齐。
- 在用户 libc 中新增最小 socket wrapper（`socket`/`bind`/`sendto`/`recvfrom`）和对应头，保持 freestanding-safe。
- 新增默认关闭的 socket 验证 smoke（用户态闭环或内核内部端到端），覆盖 socket 创建、bind、sendto/recvfrom、错误路径（未 bind、地址非法、缓冲越界、表满、无数据）和默认启动不依赖网络。
- 不引入 TCP/stream socket、`AF_*`/`SOCK_*` 全量族、`connect`/`listen`/`accept`/`getsockopt`/`setsockopt`/`poll`/`select`/`shutdown`、非阻塞标志矩阵、scatter-gather、ancillary data、完整 POSIX socket ABI、DNS 解析或网络配置工具。

## Capabilities

### New Capabilities

- `minimal-socket-interface`: 定义 BigOS 用户可见的最小有界 UDP socket 接口，覆盖 socket fd 对象模型、socket 系列 syscall 边界、用户态地址/缓冲拷贝契约、与既有 fd 表/`fork`/`close-on-exec` 的集成、错误与诊断语义，以及默认关闭验证边界。

### Modified Capabilities

- 无。

## Impact

- 影响内核 syscall 层（`kernel/core/syscall`）、进程/fd 层（`kernel/core/proc`）、新增 socket backend（预计置于 `kernel/core/net` 或 `kernel/core/ipc` 下以复用既有 `**.cc` glob 构建接入）、公开内核头（`include/bigos/syscall.h` 及新增 socket 头）、用户 libc（`user/libc`）和默认关闭 smoke 入口。
- 依赖既有 `bounded-network-protocol-path` 暴露的内核内部 UDP API（`udp_bind`/`udp_close`/`udp_send_to`/`udp_receive_from`/`pump`/`init_default`）与单一 `default_context()`，不改变协议层的有界容量边界或需求；socket 层是该内核 API 到 fd/syscall 的适配层。
- 新增 syscall number 必须同步更新 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 并保持相等（既有 tests 源契约会校验两者一致）；不改变既有 syscall number 取值或语义。
- 不改变启动地址、链接地址、页表布局、磁盘布局、IDT/syscall vector、CR3 切换或既有 fd/VFS/pipe ABI；不改变默认用户态程序集合或默认启动依赖，默认启动在无网络后端时仍正常进入 shell。
- 架构假设为当前 x86_64 freestanding 内核（UEFI 为默认启动 backend，Legacy BIOS 为交叉验证 backend）；内存使用必须通过显式内核分配路径或有界静态/栈缓冲；工具链以 xmake 与 x86_64-elf GCC 为准，辅助 Python 验证通过 `uv run ...`。
- 受协议层现状约束：内核内部 UDP 容量有界（`UDP_ENDPOINT_CAPACITY = 4`，单 `default_context`），且 RX 仅在 `pump` 被显式驱动时入队、`udp_receive_from` 为非阻塞队列轮询；本变更需在 socket 层显式定义 RX 推进与（有界）阻塞/无数据语义，而非声称通用 socket 行为。
