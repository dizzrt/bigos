## Context

BigOS 当前已有两层网络能力：M11.1 的 modern-only virtio-net 驱动暴露 frame-level `bigos::device::NetworkDevice` 接口；M11.2 的 `bigos::net` 内核内部协议路径在其上实现以太网/ARP/IPv4/ICMP/UDP 的有界处理，并提供内核内部 UDP API（`udp_bind`/`udp_close`/`udp_send_to`/`udp_receive_from`），以及 `init_default`、`default_context`、`pump`、`inject_frame`。这些能力默认关闭、容量有界、freestanding-safe，且刻意不暴露任何用户态 socket/fd/syscall。

用户态侧，BigOS 已有成熟的 fd 对象模型：每个 fd 绑定一个 `vfs::File`，`File` 通过 `const FileOperations *ops` 函数指针表实现多态（`read`/`write`/`close`/`lseek`/`truncate`/`readdir`），backend 实例状态挂在 `File::private_data`。pipe（`kernel/core/ipc/pipe.cc`）是与 socket 最接近的范本：它用两张静态 `FileOperations` 表、`File::private_data` 持有 `PipeEnd*`、用 `sched::WaitQueue` 做阻塞、用 ops 指针身份判断 fd 类型，并由 `sys_pipe` 走 `install_fd_current` + 失败回滚。syscall 走 `int 0x80` 单一 `switch (frame->rax)` 分发，参数从 `rdi/rsi/rdx/r10/r8/r9` 读取，当前最高号 `SYS_UTIMENS = 54`，number 在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 双份维护并由 tests 源契约校验相等。用户缓冲通过 `validate_user_buffer`/`copy_current_user_buffer`/`copy_to_current_user_buffer` 校验拷贝，上限 `SYS_IO_MAX_LEN = 512`，恰与 `UDP_MAX_PAYLOAD = 512` 对齐。

M11.3 的目标是把已有内核内部 UDP API 适配成最小用户可见 socket 接口。它跨越 syscall、proc/fd、网络协议三个子系统边界，且需要决定 RX 推进与阻塞语义，因此值得在编码前固化技术决策。本变更不改变启动地址、链接地址、页表布局、磁盘布局、IDT/syscall vector、CR3 切换或既有 fd/VFS/pipe ABI。

## Goals / Non-Goals

**Goals:**

- 在既有 fd/syscall 路径上提供最小用户可见 UDP socket：创建、bind 本地端口、sendto 指定 IPv4:port、recvfrom 获取来源地址与 payload。
- 把 socket 表示为一个 `vfs::File` backend，使其自然复用既有 fd 表分配、`dup`/`dup2`、`close`、`close-on-exec` 与 `fork` 时的 `vfs::retain`/`release` 引用计数路径。
- 复用既有用户缓冲校验/拷贝边界与 `SYS_IO_MAX_LEN` 上限，定义有界、确定性的用户态地址结构（仅 IPv4 + port）。
- 明确定义 socket 层的 RX 推进策略与有界阻塞/无数据语义，使其建立在协议层非阻塞 `udp_receive_from` + 显式 `pump` 之上。
- 提供默认关闭 smoke 覆盖正常闭环与错误路径，并保证默认启动在无网络后端时仍正常进入 shell。

**Non-Goals:**

- 不实现 TCP/stream socket、`connect`/`listen`/`accept`/`shutdown`、`getsockopt`/`setsockopt`、`poll`/`select`/`epoll`、scatter-gather（`sendmsg`/`recvmsg`）、ancillary data、`SO_REUSEADDR` 等选项矩阵。
- 不实现完整 `AF_*`/`SOCK_*` 枚举族、完整 POSIX `sockaddr`/`sockaddr_in` 二进制布局或完整 errno 映射矩阵；只提供 BigOS 风格的有界子集。
- 不改变协议层的有界容量（ARP/UDP endpoint/RX 队列容量、单 `default_context`）或 virtio-net 驱动边界。
- 不引入后台无界 RX 线程、动态网络配置、DNS、DHCP、多 context/多网卡 socket 选择。
- 不引入 hosted runtime、异常、RTTI 或非 freestanding 库；用户态只用本仓库有界 libc。

## Decisions

### 决策 1：socket 作为 `vfs::File` backend，沿用 pipe 的 ops-table 多态模式

把每个 UDP socket 实现为一个新的内核内部 backend：定义静态 `vfs::FileOperations SOCKET_OPS`，`File::private_data` 指向一个 socket 状态结构（持有所属 `net::Context*` 与 `net::UdpEndpoint*` 以及绑定状态）。socket fd 因此天然走既有 `install_fd_current` 分配、`dup`/`dup2`、`close`、`close-on-exec`、`fork` 的 `retain`/`release` 路径；`vfs::release` 在最后一次引用时调用 `SOCKET_OPS.close` 回收 endpoint。fd 类型判别沿用项目惯例：比较 `file->ops` 是否等于 `&SOCKET_OPS`（不引入 type enum/tagged union）。

- 备选：在 `Process` 上新增独立 socket 表与独立生命周期管理。否决，因为会重复实现 fd 分配/fork/close 语义，且与既有 fd 模型割裂。
- 备选：复用 pipe 结构。否决，UDP datagram 语义（来源地址、面向消息、bind 端口）与字节流 pipe 不同，需要独立 backend。

源码放置：新增 backend 置于 `kernel/core/net/` 或 `kernel/core/ipc/` 下，复用 `xmake/kernel.lua` 既有 `kernel/core/**.cc` glob，无需修改构建脚本即可编译。

### 决策 2：socket 系列 syscall 从 55 起，read/write 走既有 fd 路径

新增 syscall：`SYS_SOCKET`、`SYS_BIND`、`SYS_SENDTO`、`SYS_RECVFROM`，从 `55` 起连续分配，并在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 双份同步、保持相等。`close` 复用 `SYS_CLOSE`；socket fd 也可被 `dup`/`dup2`/`fcntl` 处理。

`SOCKET_OPS.read`/`SOCKET_OPS.write` 的语义：默认不把无地址的 `read`/`write` 当作网络收发（UDP 无连接、缺目的地址），统一返回确定性错误（如 `-EOPNOTSUPP` 或 `-ENOTCONN` 子集），把收发集中在 `sendto`/`recvfrom`，避免引入隐式 `connect` 默认对端。这一选择保持语义清晰且有界。

- 备选：用 `ioctl`/单个多路 `socketcall` 复用一个号。否决，BigOS syscall 风格是每操作一个稠密 number 的 `switch`，单独 number 更易读、易测、易于 sys_nr 契约校验。
- 备选：复用 `read`/`write` 做收发并预设默认对端。否决，会引入隐式连接状态，超出最小 UDP 边界。

参数约定（遵循 `rdi/rsi/rdx/r10/r8/r9`）：
- `SYS_SOCKET(domain, type, protocol)` → 校验为 BigOS UDP 子集（如 `domain=AF_INET_LITE`、`type=SOCK_DGRAM_LITE`、`protocol=0/UDP`），成功返回 fd，否则确定性负 errno。
- `SYS_BIND(fd, const sockaddr_lite*, addrlen)` → 校验 fd 为 socket、地址结构定长、调用 `net::udp_bind`，映射 `AlreadyBound`/`TableFull` 等到确定性 errno。
- `SYS_SENDTO(fd, const void* buf, len, const sockaddr_lite* dst, addrlen)` → 校验缓冲 ≤ `UDP_MAX_PAYLOAD`，拷入有界内核缓冲，调用 `net::udp_send_to`。
- `SYS_RECVFROM(fd, void* buf, len, sockaddr_lite* src_out, socklen* addrlen_io)` → 调用 RX 推进 + `net::udp_receive_from`，把来源 IPv4/port 写回用户。

### 决策 3：定长 BigOS 风格地址结构，复用既有缓冲校验

定义最小定长地址结构（概念上 `struct bigos_sockaddr_in { uint16_t family; uint16_t port; uint32_t addr; }`，host 序，字段语义在头中显式注明），避免 POSIX `sockaddr` 变长/`sa_family_t`/网络序复杂度。所有用户指针通过既有 `validate_user_buffer`/`copy_current_user_buffer`（读）与 `validate_user_io_buffer`/`copy_to_current_user_buffer`（写）处理，payload 上限对齐 `SYS_IO_MAX_LEN = 512` 与 `UDP_MAX_PAYLOAD = 512`；`addrlen` 必须等于结构定长，否则返回 `-EINVAL`。

- 备选：完整 `sockaddr`/`sockaddr_in` 二进制兼容布局。否决，超出最小边界且引入网络序/对齐细节，对当前 freestanding libc 收益低。

### 决策 4：RX 推进与有界阻塞语义

协议层现状：`udp_receive_from` 是非阻塞队列轮询（无数据返回 `Status::NoData`），RX 仅在 `pump` 被显式驱动时从设备拉帧入端口队列。socket 层必须显式定义推进与阻塞策略：

数据流（recvfrom）：在 ordinary（可阻塞）内核上下文中，先调用一次 `net::pump(default_context, bounded_max_frames)` 推进协议，再 `net::udp_receive_from`；若 `NoData`，按有界策略循环：`pump` → 检查 → 若仍无数据则 `sched::sleep`/让出一个有界时间片，直到收到数据或达到有界超时，最终返回数据或 `-EAGAIN`/`-EWOULDBLOCK` 子集。首期采用「有界轮询 + 让出」而非把协议端口接到 `WaitQueue`，因为协议层 RX 入队不在 IRQ 上下文触发 wakeup，引入 wait queue 需要改动协议层唤醒路径，超出本变更最小边界。是否提供真正阻塞由有界超时与（可选）非阻塞标志控制，默认行为在文档与 spec 中显式声明为「有界、非通用 POSIX 阻塞」。

数据流（sendto）：拷贝用户 payload 到有界内核缓冲 → `net::udp_send_to`（其内部完成 ARP 解析/IPv4/以太网构造与同步 TX）→ 映射 `ArpUnresolved`/`NoRoute`/`TooLarge`/`Timeout`/`DeviceTxFailure` 到确定性 errno。

所有 socket 操作均在 ordinary 内核上下文执行，绝不在 IRQ 上下文调用协议层；遵循协议层既有 `ordinary_context` 约束。

- 备选：后台 RX 线程持续 `pump`。否决，引入无界后台活动与额外并发面，超出最小边界。
- 备选：把端口接到 `WaitQueue` 实现真正阻塞。否决（首期），需要改动协议层在收到目标端口数据时唤醒，留作后续 change。

### 决策 5：容量与单 context 边界，多 socket 复用既有协议端口表

socket 层不新增独立网络容量，直接映射到协议层单 `default_context` 的 `UDP_ENDPOINT_CAPACITY = 4` 端口表。超出时 `bind`/`socket` 返回确定性 `-EMFILE`/`-ENOSPC` 子集（取决于是 fd 表满还是协议端口表满）。socket 在未 `bind` 前可创建但 `recvfrom` 返回确定性错误（无本地端口）。`fork` 后子进程通过 `retain` 共享同一 `File`/endpoint 引用，关闭语义遵循引用计数。

## Risks / Trade-offs

- [Risk] 有界轮询式 recvfrom 不是真正的 POSIX 阻塞，长时间无数据会消耗 CPU 时间片 → Mitigation：在 spec/文档显式声明为有界非通用阻塞；recvfrom 采用有界超时 + `sched` 让出，避免忙等独占；后续可单独 change 引入 wait-queue 唤醒。
- [Risk] 单 `default_context` + 4 端口容量限制并发 socket 数 → Mitigation：明确为 M11.3 有界边界，`bind`/`socket` 满载返回确定性 errno；多 context/多网卡留作后续。
- [Risk] 新增 syscall number 在两份头文件不同步会破坏用户态 ABI 与 tests 契约 → Mitigation：把双份同步与 number 相等作为显式任务与验证项，跑既有 sys_nr 源契约测试。
- [Risk] `read`/`write` 对 socket fd 的语义若不明确会造成误用 → Mitigation：明确 socket 的 `read`/`write` 返回确定性不支持错误，收发只走 `sendto`/`recvfrom`。
- [Risk] socket smoke 依赖 QEMU/tap/MSI-X 真实网络后端，宿主可能不可用 → Mitigation：优先用内核内部端到端/注入式验证（复用协议层 `inject_frame` 思路）做可在无 tap 环境运行的闭环；真实 tap 路径不可用时记录跳过原因与剩余风险。
- [Risk] socket backend 在 `fork`/`close`/错误回滚路径上 endpoint 双重释放或泄漏 → Mitigation：严格沿用 pipe 的 `retain`/`release` + `install_fd_current` 失败回滚范式，并在任务中加生命周期/引用计数审查。

## Migration Plan

1. 在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 同步新增 socket 系列 number（从 55 起）与有界地址结构/常量定义，保持两份相等。
2. 新增 socket backend（`SOCKET_OPS`、socket 状态结构、`create`/`is_socket_file`），适配内核内部 `bigos::net` UDP API。
3. 在 `kernel/core/syscall` 的 `dispatch` switch 中新增 socket 系列 case（置于既有 `BIGOS_USER_PROCESS` 守卫内），完成用户缓冲/地址校验、错误映射与 fd 安装/回滚。
4. 在 `user/libc` 新增 `socket`/`bind`/`sendto`/`recvfrom` wrapper 与头，errno 翻译沿用既有 `errno_translate`。
5. 新增默认关闭 build switch 与 socket smoke 入口；默认关闭时不改变默认启动与默认用户态程序集合。
6. 更新 docs/en 与 docs/zh 镜像，描述最小有界 socket 边界（不声称完整 POSIX socket 或完整网络栈）。
7. 实现并验证完成后，将 roadmap 的 M11.3 标记为已完成，保持 roadmap 仅项目规划级描述。

Rollback 策略：关闭新增 build switch 即可移除 socket smoke；移除 socket 系列 syscall case 与 backend 注册即可回到「仅内核内部 UDP API」状态。由于不改变既有 syscall number、fd/VFS/pipe ABI、boot/页表/磁盘布局，回滚不需要既有用户态迁移；新增 number 是追加式的，回滚时需同步从两份头中移除以保持相等。

## Open Questions

- 无（首期采用有界轮询式 recvfrom；真正 wait-queue 阻塞与多 context 作为后续独立 change）。
