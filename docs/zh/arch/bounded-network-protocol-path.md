# 有界网络协议路径

BigOS 在 frame-level `NetworkDevice` 接口之上提供内核内部有界网络协议路径。该路径处理
Ethernet II 分发、有界 ARP cache 与 pending resolution、无分片 IPv4 校验、ICMPv4
echo reply，以及仅供内核使用的 UDP datagram endpoint table。

协议 context 仅支持单接口。初始化要求已有 ready 的 frame-level 网络设备，并由内核调用方提供静态
IPv4 配置。缺少设备或配置无效时，context 会以确定性诊断保持 disabled；默认启动、storage、filesystem、`/rw`、shell 和 userland 不依赖网络可用性。

该协议模块保持有界且只支持单 context：不暴露 `/dev` 网络节点、DHCP、DNS、IPv6、IP 分片重组、NAT、防火墙、动态路由或多接口路由。用户可见 socket 是该协议路径之上的显式有界适配层：既有 UDP datagram 接口与最小 TCP stream socket 接口在 `docs/zh/arch/syscall-entry.md` 中说明。它们不改变协议模块的定容边界或更广义的非目标。

协议解析只在普通内核上下文运行。Virtio-net MSI-X handler 仍只处理 frame-level RX/TX 完成状态；protocol pump 与 UDP endpoint 操作会拒绝 IRQ/nonblocking context。RX buffer 在 frame 处理结束后 exactly-once 归还，TX 成功只来自底层 network-device transmit 结果。

验证通过默认关闭的 bounded network protocol smoke case 执行。它覆盖初始化、ARP request/reply 状态、IPv4 校验、ICMP echo、UDP bind/send/receive 和 unsupported-frame 拒绝，同时不让 normal boot 依赖宿主网络后端。

## 本机地址 Loopback 路径

IPv4 输出层对目的地址分类，为本机地址流量提供一条内核内部 loopback 路径。本机地址集合为配置的本机 IPv4 地址加上整个回环网段 `127.0.0.0/8`（不仅是 `127.0.0.1`）。当 `send_ipv4` 为本机地址目的构造好报文时，直接把报文交给 IPv4 输入分发处理，而不进行 ARP 解析或经帧级设备发送；其余目的地址保持既有 `route_destination` + ARP + 帧级设备路径不变。

本机投递把源/宿地址域归一化到 `local_ipv4`（`127.0.0.1` 视为 `local_ipv4` 的别名进入输入路径），使既有 UDP 伪首部与 IPv4 校验和逻辑无需特例即可自洽。本机 UDP datagram 复用与入站帧相同的有界 UDP endpoint RX 队列与就绪唤醒；队列满与端口未绑定返回相同的确定性状态。本机 ICMP echo-to-self 有界：一个 echo request 恰好产生一次 echo reply，reply 再进入输入路径被识别为 reply、不再生成新的 request（递归深度至多一次回声）。

本机地址路径不要求 ready 帧级设备。当 context 有有效本机 IPv4 配置但无 ready `NetworkDevice` 时，进入 loopback-only 就绪模式：本机地址 bind/send/receive 可用，而对外（非本机）发送仍返回确定性 not-ready/设备状态。未配置的默认启动不会进入该模式，因此默认启动、storage、filesystem、`/rw`、shell 与 userland 与网络路径无关。

两个 append-only 诊断计数 `loopback_delivered` 与 `loopback_dropped` 在不改动既有计数的前提下区分“本机 loopback 命中”与“对外帧级命中”。loopback 路径不新增用户可见 syscall/fd/socket ABI、无 `lo` 设备节点、无接口枚举，也不引入通用 routing/转发或多接口模型；用户程序仅通过既有 UDP socket 面向本机地址收发即可受益。验证通过默认关闭的 `loopback_network_smoke` build switch 执行，在 COM1 输出 `BIGOS_LOOPBACK_NETWORK_PASSED` / `BIGOS_LOOPBACK_NETWORK_FAILED`，覆盖 UDP loopback 闭环、ICMP echo-to-self 以及 unbound/queue-full/非本机错误路径，且无需真实 tap 或网卡。

## 有界 TCP 路径

协议路径在同一 IPv4 层之上承载一个内核内部有界 TCP 状态机。IPv4 输入分发把协议号 6（TCP）分发给 `handle_tcp`，TCP 段经既有 `send_ipv4` 输出层发送：本机地址 TCP 段复用 loopback 分流，对外段复用 `route_destination` + ARP + 帧级设备，不另建第二套 IPv4 输出或校验逻辑。TCP 校验和覆盖 IPv4 伪首部（源/宿 IPv4、协议号 6、TCP 长度）加 TCP 头与数据；本机地址闭环时伪首部宿地址与 IPv4 头一样归一化到 `local_ipv4`，使 `handle_tcp` 重建校验和自洽、无需特例。

TCP 状态保存在编译期定容的连接控制块（TCB）池中。每个 TCB 持有连接四元组、`TcpState`（Closed/Listen/SynSent/SynReceived/Established/FinWait1/FinWait2/Closing/CloseWait/LastAck/TimeWait）、定容发送/接收缓冲、定容重传队列与定容乱序重组窗口。连接按四元组精确匹配。被动打开采用 Linux/BSD 风格 listener + 子 TCB 模型：`LISTEN` TCB 保持 `Listen` 并按本地端口匹配；入站 SYN 派生处于 `SynReceived` 的子 TCB 并进入有界 SYN queue，最终 ACK 使已建立子连接移入 listener 的有界 accept queue。序号比较采用 32 位回绕安全的有符号差值运算。连接池、缓冲、重传队列、重组窗口、SYN queue 与 accept queue 都不超过编译期上界：池满或队列满会确定性丢弃/拒绝超额连接并计一次丢弃。

重传遵循 RFC 6298，且仅用整数/移位运算（无浮点）：估计器维护 `SRTT`/`RTTVAR`（alpha=1/8、beta=1/4），计算 `RTO = SRTT + max(G, 4*RTTVAR)` 并 clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`（对齐 Linux 200ms / 120s）。RTT 采样遵循 Karn 算法（重传过的段不采样），超时按指数退避重传（`RTO = min(RTO*2, TCP_RTO_MAX)`）且不改写 `SRTT`/`RTTVAR`，超过有界重传上限即确定性复位连接。流控以接收缓冲剩余空间作为通告窗口；缓冲满时通告有界（含 0）窗口，不无界缓冲。数据交付由序号驱动：按序数据推进 `rcv_nxt`，落在窗口内的乱序段缓存进定容重组窗口并在缺口填补后合并，重复/超窗/校验失败段确定性丢弃且不破坏已交付数据。重传、RTT 采样与 `TIME_WAIT` 回收只在普通（非 IRQ）上下文运行，沿用 ARP pending 式 tick 驱动推进；绝不在 IRQ 上下文分配、阻塞或操作 TCB 缓冲。

连接拆除覆盖主动关闭（FIN -> FinWait1 -> FinWait2/TimeWait）、被动关闭（CloseWait -> LastAck）与同时关闭（Closing）。`TIME_WAIT` 使用标准 `2*MSL`（`2*TCP_MSL_TICKS`）后回收 TCB；当定容池已有 `TIME_WAIT` 槽占用时，新建连接返回确定性表满状态而非提前回收该槽。匹配的 RST、重传超限或不可恢复的非法段会复位连接并回收 TCB，不泄漏其定容缓冲。append-only TCP 诊断计数（`tcp_segments_rx`/`tcp_segments_tx`/`tcp_retransmits`/`tcp_connections_opened`/`tcp_connections_closed`/`tcp_resets`/`tcp_dropped`）位于 loopback 计数之后、`last_status` 之前，由 `static_assert` 偏移检查守护，不改变既有计数语义。

该能力是第一步 TCP 兼容能力：实现连接建立、RFC 6298 动态重传、有序交付/重组与连接拆除。拥塞控制、SACK、窗口缩放、时间戳/PAWS、urgent 指针、keepalive 与更广 POSIX socket 行为属于后续分阶段扩展，而非永久非目标。当前用户可见 TCP stream socket 适配层暴露 `docs/zh/arch/syscall-entry.md` 中记录的 syscall/fd 表面；名字解析与更广 socket 行为由后续 resolver/socket 兼容工作扩展。验证通过默认关闭的 `tcp_path_smoke` build switch 执行，在 COM1 输出 `BIGOS_TCP_PATH_PASSED` / `BIGOS_TCP_PATH_FAILED`，覆盖本机地址三次握手（主动+被动）、`Established` 有序双向数据交付、有界重组与重复丢弃、带退避与 Karn 采样的 RFC 6298 重传路径、重传超限复位、带标准 `2*MSL` `TIME_WAIT` 回收的拆除、连接表满路径与非法段——全部在 loopback 就绪的协议路径上完成，无需真实 tap 或网卡。默认启动不依赖 TCP 或 stream socket。
