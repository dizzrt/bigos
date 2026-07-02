## Why

内核内部网络协议路径（`bigos::net`：Ethernet/ARP/IPv4/ICMP/UDP）目前只承载无连接 datagram 语义：UDP 端点把一条报文原样入队定容 RX 队列即完成，没有连接状态、没有序号、没有确认与重传、没有有序重组。面向连接的网络能力（真实 TCP client/server 交互、`connect` 到本机地址、有序可靠字节流）缺少最底层的一块：一条内核内部的有界 TCP 协议路径。

上一步已落地本机地址 loopback 路径，使面向连接与 datagram 的本机地址流量无需物理或仿真网卡即可在协议层闭环，为可复现、默认关闭的验证提供了确定性基座。在此之上需要先构建 TCP 协议状态机本身（连接建立、有界重传与窗口、有序交付、连接拆除），它是后续 stream socket 用户接口（`connect`/`listen`/`accept`）与名字解析能力的前置依赖。本变更聚焦协议路径，不暴露新的用户可见 socket ABI。

## What Changes

- 新增一条内核内部有界 TCP 协议路径：在既有 IPv4 输入分发中识别并处理 TCP 段（IP 协议号 6），并新增 TCP 段输出，承接连接建立、数据传输、连接拆除，复用既有 IPv4 输出层（含本机地址 loopback 分流），使本机地址 TCP 段无需 ARP/帧级设备即可闭环。
- 引入有界连接控制块（TCB）池与 TCP 状态机：定容连接表，覆盖 `CLOSED`/`LISTEN`/`SYN_SENT`/`SYN_RECEIVED`/`ESTABLISHED`/`FIN_WAIT_1`/`FIN_WAIT_2`/`CLOSING`/`CLOSE_WAIT`/`LAST_ACK`/`TIME_WAIT` 状态迁移，实现三次握手连接建立、有序数据交付与 FIN/ACK 连接拆除。
- 有界重传与窗口：每个 TCB 使用定容发送/接收缓冲与定容重传队列；基于序号/确认号推进；重传超时按 RFC 6298 动态 RTT 估计计算（维护 SRTT/RTTVAR、`RTO = SRTT + max(G, K*RTTVAR)`、Karn 算法、超时指数退避，RTO clamp 到对齐 Linux 的 `[200ms, 120s]`），有界最大重传次数超限即以确定性状态复位/放弃连接；通过通告窗口做有界流控，接收缓冲满时不无界缓冲。
- 有序交付与重组：以序号驱动，对乱序到达的段在定容重组窗口内有界缓存并按序交付；重复、超窗、越界或校验失败的段以确定性状态丢弃；ACK 累积确认，推进发送侧重传队列回收。
- 连接拆除与资源回收：正常 FIN 双向拆除、被动关闭（`CLOSE_WAIT`→`LAST_ACK`）、同时关闭（`CLOSING`）与标准 `TIME_WAIT`（对齐 Linux 的 `2*MSL`，超时后回收 TCB）；异常路径（RST、重传超限、非法段）以确定性状态复位并回收 TCB。
- 计时与上下文边界：TCP 的重传/`TIME_WAIT` 超时复用既有 monotonic tick 与协议路径的 ordinary（非 IRQ）上下文推进模型（沿用 ARP pending 超时的推进方式），不在 IRQ 上下文分配内存、阻塞或操作连接缓冲。
- 新增一个默认关闭的运行期 smoke 开关与 COM1 标记：在无 tap/无网卡（loopback 就绪）环境下验证本机地址 TCP 连接建立、有序双向数据交付、乱序/重复段的确定性处理、重传路径、正常与异常连接拆除、连接表满/非法段的确定性行为，以及默认启动不依赖 TCP。

## Capabilities

### New Capabilities
- `bounded-tcp-path`: 内核内部有界 TCP 协议路径。提供定容 TCB 池与 TCP 状态机、三次握手连接建立、按 RFC 6298 的动态 RTT 估计与有界重传、窗口流控、序号驱动的有序交付与重组、正常/被动/同时关闭与标准 `2*MSL` `TIME_WAIT` 连接拆除，以及 RST/重传超限/非法段的确定性复位。TCP 段经既有 IPv4 输出层输出、经既有 IPv4 输入分发接收，本机地址 TCP 段复用 loopback 闭环。该能力保持有界：不实现完整 TCP 特性矩阵（无拥塞控制算法、无 SACK、无窗口缩放、无时间戳/PAWS、无 urgent 指针、无 keepalive 特性矩阵），不引入无界缓冲，不暴露新的用户可见 syscall/fd/socket 语义（stream socket 用户接口与名字解析为后续独立能力）。

### Modified Capabilities
- `bounded-network-protocol-path`: 扩展 IPv4 输入分发以识别 TCP（协议号 6）并交给 TCP 段处理，扩展 IPv4 输出以承载 TCP 段（本机地址目的经既有 loopback 分流、对外目的经既有 `route_destination` + ARP + 帧级设备发送），并在诊断结构末尾追加 TCP 相关计数字段，用于确定性区分 TCP 段的接收/发送/丢弃/重传/状态迁移；既有 Ethernet/ARP/IPv4/ICMP/UDP 处理、malformed 丢弃、IRQ 边界与既有诊断字段取值语义保持不变。

## Impact

- 受影响内核子系统：内核内部网络协议路径。IPv4 输入分发（`kernel/core/net/protocol.cc` 的 `handle_ipv4` 协议号分发新增 TCP 分支 `handle_tcp`）、IPv4 输出（`send_ipv4` 承载 TCP 段并复用既有本机地址 loopback 分流与对外发送路径）；新增 TCP 状态机与 TCB 管理（预期为 `kernel/core/net/tcp.cc` 与 `include/bigos/net/tcp.h`，保持公共头精简）；诊断结构（`include/bigos/net.h` 的 `Diagnostics` 末尾 append-only 追加 TCP 计数字段，`static_assert` 守护既有字段偏移不变）。
- 复用而非改动既有原语：既有 IPv4 头部/长度/分片/校验和校验、本机地址判定与 loopback 分流（`is_local_delivery` + `send_ipv4` 本机分支）、ordinary-context 边界（`ordinary_context()`/`UnsupportedContext`）、monotonic tick 与 ARP pending 式的超时推进（`pump`/tick 驱动）、`Status` 枚举与确定性诊断计数模式；TCP 是对现有输入/输出/推进路径的追加式分支与新增状态机。
- ABI/接口影响：不新增、不改动任何 syscall 编号、寄存器传参顺序、`int 0x80` 返回语义或用户可见 socket ABI；本变更仅提供内核内部 TCP 协议路径，不暴露 `connect`/`listen`/`accept` 用户接口（留待后续 stream socket 能力）。
- 构建/验证：新增一个默认关闭 xmake smoke 开关（映射到 `BIGOS_*` 宏）与对应 COM1 标记，遵循既有 `network_protocol_smoke`/`socket_smoke`/loopback smoke 模式，通过 QEMU headless 路径验证；内核内部 TCP 闭环基于本机地址 loopback 就绪，不依赖 tap/真实网卡；不改动默认启动行为。
- 非目标：不实现完整 TCP 特性矩阵——不实现拥塞控制算法（Reno/CUBIC 等）、慢启动/拥塞避免曲线、SACK、窗口缩放、时间戳/PAWS、urgent 指针、Nagle/delayed-ACK 调优矩阵、keepalive 特性（RTO/RTT 估计按 RFC 6298 实现，属本变更目标，不在此排除）；不暴露 stream socket 用户接口（`connect`/`listen`/`accept`）或任何新用户可见 syscall/fd 语义；不实现名字解析/DNS；不实现通用 IP routing/转发或多接口地址模型；不引入无界缓冲或用户可见 async I/O；不改动既有 UDP/ICMP/ARP 行为、帧级设备 TX/RX ownership 或 IRQ 边界；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 交叉工具链与 xmake；QEMU headless 为主要 smoke 路径；TCP 内核内部闭环基于本机地址 loopback，不依赖 tap 权限或真实/仿真网卡；连接数、缓冲与重传队列容量均为编译期定容上界；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
