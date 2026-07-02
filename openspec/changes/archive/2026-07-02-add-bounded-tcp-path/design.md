## Context

内核内部网络协议路径 `bigos::net`（`kernel/core/net/protocol.cc`）已实现 Ethernet/ARP/IPv4/ICMP/UDP 的有界处理，输入分发按 IPv4 协议号在 `handle_ipv4` 中分发（当前仅 ICMP=1、UDP=17），输出统一走 `send_ipv4`。上一步的本机地址 loopback 路径已使 `send_ipv4` 在目的为本机地址集合（`local_ipv4` 或 `127.0.0.0/8`）时把报文直接交给 `handle_ipv4` 闭环，无需 ARP/帧级设备，并在无 ready `NetworkDevice` 但有有效本机配置时进入 `State::LoopbackReady`。这给面向连接的协议提供了确定性、默认关闭、无需 tap/网卡的验证基座。

当前缺口：协议路径没有任何连接状态或可靠有序字节流语义。UDP 端点是「入队一条 datagram 即完成」的无连接模型，没有序号、确认、重传、窗口与重组。面向连接网络能力（真实 TCP client/server、`connect` 到本机地址）需要先在协议层构建 TCP 状态机本身。

现有关键约束与可复用点：

- `handle_ipv4`（`kernel/core/net/protocol.cc`）已完成 IPv4 头部/版本/IHL/长度/分片/校验和校验，并把 body 交协议号分发；本机过滤已放宽为 `is_local_delivery(ctx,dest) || BROADCAST`。TCP 只需在其分发点追加协议号 6 分支。
- `send_ipv4` 使用固定栈缓冲 `packet[20 + UDP_MAX_PAYLOAD + 8]` 承载 IPv4 头 + 载荷，并内建本机地址 loopback 分流与对外 ARP/设备发送；TCP 段作为 IPv4 载荷复用该输出层，段大小必须受该缓冲上界约束。
- 超时推进模型：ARP pending 使用 `deadline_tick` + `pump`/`timer::ticks()` 在 ordinary 上下文推进；TCP 的重传与 `TIME_WAIT` 超时复用同一 tick 与 ordinary-context 推进方式，不引入 IRQ 上下文定时器回调。
- 上下文边界：协议输出/投递 MUST 在 ordinary（可阻塞、非 IRQ）上下文执行；`ordinary_context()`/`UnsupportedContext` 已是既有边界。
- freestanding-safe、x86_64-only、无 hosted libc；容量/缓冲/连接数保持编译期定容；不改动既有 syscall/socket ABI；不改动 boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

本变更聚焦「协议层 TCP 状态机」，不暴露 `connect`/`listen`/`accept` 用户接口（留待后续 stream socket 能力），因此内部通过内核内部 API（新增 `bigos::net::tcp_*` 或等价内部入口）驱动，验证走默认关闭 smoke。

## Goals / Non-Goals

**Goals:**

- 在协议层新增有界 TCP：定容 TCB 池 + TCP 状态机，覆盖 `CLOSED`/`LISTEN`/`SYN_SENT`/`SYN_RECEIVED`/`ESTABLISHED`/`FIN_WAIT_1`/`FIN_WAIT_2`/`CLOSING`/`CLOSE_WAIT`/`LAST_ACK`/`TIME_WAIT` 迁移。
- 实现三次握手连接建立（主动 SYN / 被动 LISTEN+SYN,ACK）、序号/确认号驱动的有序数据交付与有界重组、累积 ACK 推进发送侧重传队列回收、有界通告窗口流控。
- 实现符合 RFC 6298 的动态 RTT 估计与重传：维护 `SRTT`/`RTTVAR`，按 `RTO = SRTT + max(G, K*RTTVAR)`（K=4）计算 RTO 并 clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`，采样使用 Karn 算法（重传段不采样 RTT），超时后按指数退避加倍 RTO；重传次数超过上界以确定性状态复位/放弃连接。定容重传队列保持有界。
- 实现连接拆除：正常 FIN 双向、被动关闭（`CLOSE_WAIT`→`LAST_ACK`）、同时关闭（`CLOSING`）、标准 `TIME_WAIT`（2*MSL）超时回收；RST/非法段确定性复位并回收 TCB。
- TCP 段经既有 `send_ipv4` 输出、经 `handle_ipv4`→`handle_tcp` 接收，本机地址 TCP 段复用既有 loopback 闭环，使内核内部 TCP 在 `LoopbackReady`（无设备）下可闭环验证。
- 追加 TCP 诊断计数（append-only）并提供默认关闭 smoke 与确定性 COM1 marker。

**Non-Goals:**

- 不实现完整 TCP 特性矩阵：无拥塞控制算法（Reno/CUBIC）、无慢启动/拥塞避免、无 SACK、无窗口缩放、无时间戳/PAWS、无 urgent 指针、无 Nagle/delayed-ACK 调优矩阵、无 keepalive 特性。RTO/RTT 估计按 RFC 6298 实现（属本变更目标，不在此排除）。
- 不暴露 stream socket 用户接口（`connect`/`listen`/`accept`）或任何新用户可见 syscall/fd/socket 语义；不实现名字解析/DNS。
- 不实现通用 IP routing/转发、多网卡/多接口地址模型。
- 不引入无界缓冲或用户可见 async I/O；不改动既有 UDP/ICMP/ARP 行为、帧级设备 TX/RX ownership 或 IRQ 边界；不改动 boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## Decisions

### 决策一：TCP 状态机与 TCB 管理独立成 `tcp.cc`/`tcp.h`，只在协议层挂接两个点

在 `kernel/core/net`（新增 `tcp.cc`）实现 TCP 状态机与 TCB 池，并在 `include/bigos/net/tcp.h` 暴露内核内部 API（连接建立/发送/接收/关闭/推进），保持 `include/bigos/net.h` 精简（仅追加 TCP 诊断计数与必要常量）。在 `protocol.cc` 只挂接两个既有点：

```
接收: handle_ipv4 (协议号==6) -> handle_tcp(ctx, source, dest, tcp_segment, len)
输出: tcp 段 -> send_ipv4(ctx, dest, IPV4_PROTOCOL_TCP, segment, seg_len)
```

- 选择独立单元的原因：TCP 状态机体量与语义远大于 ICMP/UDP，混入 `protocol.cc` 会让文件与 `net.h` 膨胀；独立单元便于把 TCB 池、状态迁移、重传队列、重组窗口聚拢，且与既有 UDP/ICMP 分发解耦。
- 备选（塞进 `protocol.cc`）被否：`protocol.cc` 已 5 万余字节，TCP 会显著降低可读性并放大回归面。
- 备选（TCP 自建输出绕过 `send_ipv4`）被否：会旁路既有本机地址 loopback 分流与对外 ARP/设备路径，产生第二套 IPv4 输出与校验逻辑，与 loopback/对外语义漂移。TCP 段一律经 `send_ipv4` 输出，天然复用 loopback 与对外分流。

### 决策二：TCP 段输出复用 `send_ipv4`，段大小受既有 IPv4 载荷缓冲上界约束，定义有界 MSS

TCP 段作为 IPv4 载荷交 `send_ipv4`。`send_ipv4` 现有栈缓冲为 `20 + UDP_MAX_PAYLOAD + 8`，TCP 段（20 字节 TCP 头 + 数据）必须整体不超过该 IPv4 载荷上界。据此定义有界 `TCP_MSS`（TCP 数据段最大字节，取值使 `20(IP) + 20(TCP) + TCP_MSS` 不超过既有 `send_ipv4` 缓冲与 `mtu`），并在 `net.h`/`tcp.h` 以常量固化。

- 复用 `send_ipv4` 的伪首部/校验和一致性：TCP 校验和采用 IPv4 伪首部（源/宿 IPv4 + 协议号 6 + TCP 长度）+ TCP 头 + 数据。本机地址闭环时 `send_ipv4` 把宿地址归一化为 `local_ipv4`，因此 TCP 伪首部的宿地址在本机闭环下同样以 `local_ipv4` 计算，使 `handle_tcp` 重建校验和自洽（与既有 `handle_udp` 归一化处理一致）。
- 若 TCP 段超过上界则以确定性 `TooLarge` 拒绝，发送侧按 MSS 分段（有界，不做 IP 分片）。

### 决策三：定容 TCB 池 + 显式状态机，序号采用 32 位回绕比较

新增 `TcpControlBlock`（TCB）与定容连接表 `TCP_CONNECTION_CAPACITY`。每个 TCB 持有：四元组（本地/远端 IPv4+port）、当前 `TcpState`、发送侧（`snd_una`/`snd_nxt`/`snd_wnd` + 定容发送/重传缓冲）、接收侧（`rcv_nxt`/`rcv_wnd` + 定容接收缓冲 + 定容乱序重组槽）、重传计时（`rto_deadline_tick`/`retransmit_count`）、`TIME_WAIT` 计时（`time_wait_deadline_tick`）。序号比较使用 32 位模运算回绕安全比较（`(int32_t)(a-b) < 0` 语义），避免回绕误判。

- 状态迁移遵循标准 TCP 图，但只覆盖目标状态集合与有界特性；连接查找按四元组精确匹配，`LISTEN` TCB 按本地端口匹配被动打开。
- 连接表满时新连接以确定性状态拒绝（`TableFull` 或等价），不无界扩张。

### 决策四：RFC 6298 动态 RTT 估计 + Karn 算法 + 指数退避，ordinary 上下文推进

重传按 RFC 6298 实现完整动态 RTT 估计（对齐真实 Linux 做法）：

- 首次采样（首个 RTT 测量 R）：`SRTT = R`，`RTTVAR = R/2`，`RTO = SRTT + max(G, K*RTTVAR)`，K=4，G 为时钟粒度（本项目 1 tick = 10ms，`TIMER_HZ=100`）。
- 后续采样：`RTTVAR = (1-beta)*RTTVAR + beta*|SRTT - R'|`（beta=1/4），`SRTT = (1-alpha)*SRTT + alpha*R'`（alpha=1/8），再重算 `RTO = SRTT + max(G, K*RTTVAR)`。alpha/beta 用移位（1/8=>>3、1/4=>>2）实现，避免浮点（freestanding 无 FPU 依赖）。
- RTO clamp：`RTO` 下限 `TCP_RTO_MIN`（对齐 Linux 200ms => 20 ticks），上限 `TCP_RTO_MAX`（对齐 Linux 120s => 12000 ticks）。
- Karn 算法：重传过的段不用于 RTT 采样（避免重传二义性），仅对「一次发送即被确认」的段采样 RTT。
- 超时退避：RTO 到期未确认段被重传时 `RTO = min(RTO*2, TCP_RTO_MAX)`（指数退避），退避不改写 SRTT/RTTVAR；收到新 RTT 采样后按上式重新计算 RTO 收敛。
- `retransmit_count` 超过 `TCP_MAX_RETRANSMIT` 即以确定性状态复位连接（发送 RST 或直接放弃并回收 TCB）。

每次发送把未确认段登记进定容重传队列并记录 `send_tick` 与 `rto_deadline_tick`；推进函数（在 ordinary 上下文，由 tick/`pump` 驱动，沿用 ARP pending 超时的推进方式）检查到期段并重传。

- RTT 以 `timer::ticks()` 之差测量（10ms 粒度）：采样精度受 `TIMER_HZ=100` 限制，`G` 取 1 tick，`SRTT`/`RTTVAR`/`RTO` 均以 tick 为单位的整数存储。
- 复用既有 `timer::ticks()` 单调 tick 与「ordinary 上下文轮询推进」模型；MUST NOT 在 IRQ 上下文触发重传、RTT 采样、分配或缓冲操作。
- ACK 累积确认推进 `snd_una`，回收重传队列中已被确认的段，重置对应计时；对未重传过的段用其 `send_tick` 采样 RTT 更新 SRTT/RTTVAR/RTO。
- 备选（固定 RTO/简化估计）被否：用户明确要求对齐真实 Linux 的动态 RTT 估计，故实现完整 RFC 6298 估计器而非固定值。
- 备选（IRQ 定时器回调驱动重传）被否：会把连接缓冲操作引入 IRQ 上下文，违反既有 ordinary-context 边界与「IRQ 上下文不分配/不阻塞」约束。

### 决策五：有序交付与有界重组窗口

接收侧以 `rcv_nxt` 驱动：按序到达的数据直接进入定容接收缓冲并推进 `rcv_nxt`；乱序但落在接收窗口内的段进入定容乱序重组槽（`TCP_REORDER_SLOTS`），当缺口被填补时按序合并推进；重复、超出接收窗口、越界或校验失败的段以确定性状态丢弃并计数。接收窗口大小由接收缓冲剩余空间决定，作为通告窗口回送对端做有界流控；接收缓冲/重组槽满时停止推进窗口（通告 0 窗口或有界丢弃），不无界缓冲。

- 备选（无重组、乱序即丢弃仅靠重传）被否：本机 loopback 环境虽少乱序，但 smoke 需显式验证乱序段的确定性处理与有序交付语义；有界重组槽提供确定性有序交付且容量可控。

### 决策六：连接拆除与标准 TIME_WAIT 回收

正常关闭发送 FIN 进入 `FIN_WAIT_1`，按对端 ACK/FIN 迁移到 `FIN_WAIT_2`/`TIME_WAIT`；被动关闭收到 FIN 进入 `CLOSE_WAIT`，本地关闭后 `LAST_ACK`；同时关闭走 `CLOSING`。`TIME_WAIT` 使用标准 `2*MSL`（对齐真实 Linux 做法）：以编译期常量 `TCP_MSL_TICKS` 表示 MSL，`time_wait_deadline_tick = timer::ticks() + 2*TCP_MSL_TICKS`，到期回收 TCB。收到 RST、重传超限或非法段时确定性复位并立即回收 TCB。

- `TIME_WAIT` 时长说明：对齐真实 Linux 采用标准 `2*MSL`。Linux 的 `TCP_TIMEWAIT_LEN` 固定 60s（其 MSL≈30s），本项目以 `TCP_MSL_TICKS` 常量固化 MSL（tick 单位），`2*MSL` 即 `TIME_WAIT` 时长；该值不因 smoke 便利而缩短语义。
- 定容池与长 `TIME_WAIT` 的相容性：由于 TCB 池定容且 `TIME_WAIT` 时长较长，若 `TIME_WAIT` 槽占满，新连接建立按连接表满的确定性状态处理（`TableFull`），不无界扩张、不提前破坏 `TIME_WAIT` 语义。smoke 可用较短 MSL 常量配置或直接驱动 `tcp_pump` 到期以在有界时间内验证回收，但默认语义遵循标准 `2*MSL`。

### 决策七：诊断计数 append-only，`Diagnostics` 末尾追加 TCP 计数

在 `include/bigos/net.h` 的 `Diagnostics` 末尾（当前 `loopback_dropped` 之后、`last_status` 之前，或统一追加到 `last_status` 之前的新块）append-only 追加 TCP 计数：至少包含 `tcp_segments_rx`/`tcp_segments_tx`/`tcp_retransmits`/`tcp_connections_opened`/`tcp_connections_closed`/`tcp_resets`/`tcp_dropped`（乱序/超窗/校验失败/表满等确定性丢弃）。新增字段一律末尾追加，并以 `__builtin_offsetof` `static_assert` 守护既有字段偏移与 append 顺序不变，不改动既有计数取值语义。

- 增补原因：smoke 与后续验证需确定性区分 TCP 段接收/发送/重传/状态迁移/丢弃；仅靠既有 IPv4/UDP 计数无法区分 TCP 路径。

### 决策八：验证以本机地址内核内部闭环为主

新增默认关闭 smoke（`--tcp_path_smoke`，映射 `BIGOS_TCP_PATH_SMOKE` → `BIGOS_TCP_PATH_PASSED/FAILED`），在 `LoopbackReady`（仅本机配置、无帧级设备）下覆盖：本机地址 TCP 三次握手连接建立、`ESTABLISHED` 有序双向数据交付、乱序/重复段确定性处理、重传路径（构造丢段/延迟触发重传或直接驱动重传推进）、正常 FIN 双向拆除与有界 `TIME_WAIT` 回收、被动/同时关闭、RST/重传超限确定性复位、连接表满与非法段确定性行为，并断言相应 TCP 诊断计数确定性递增。通过 QEMU headless 观测确定性 COM1 marker；默认（开关关闭）构建不含 TCP smoke 线程，默认启动行为不变。

## Risks / Trade-offs

- [TCP 状态机复杂度引入回归面] TCP 是本项目最复杂的协议状态机，误实现会破坏协议路径 → 缓解：独立成 `tcp.cc`，仅在 `handle_ipv4` 追加协议号分支、经 `send_ipv4` 输出，不改既有 UDP/ICMP/ARP/loopback 路径；默认关闭 smoke 覆盖各状态迁移与错误路径；GCC 交叉构建 + QEMU headless 双 marker（TCP smoke 通过 + 默认启动回归）。
- [段大小超出既有 IPv4 载荷缓冲] `send_ipv4` 使用固定栈缓冲，TCP 段过大将越界或被拒 → 缓解：定义有界 `TCP_MSS` 使 `IP+TCP 头+MSS` 不超过既有缓冲与 `mtu`；发送按 MSS 有界分段；超限确定性 `TooLarge`；`static_assert`/编译期常量守护上界关系。
- [序号回绕误判] 32 位序号回绕比较若用普通无符号比较会误判 → 缓解：统一用回绕安全比较（有符号差值），并在 smoke 覆盖跨回绕点或至少覆盖典型序号推进；在代码注释仅记录该非显然约束。
- [RFC 6298 定点整数估计精度] SRTT/RTTVAR 用 tick 整数与移位（alpha=1/8、beta=1/4）近似，10ms tick 粒度下 loopback RTT 常≈0，可能使 RTO 收敛到 `TCP_RTO_MIN` → 缓解：`RTO = SRTT + max(G, K*RTTVAR)` 的 `max(G, ...)` 保证下界不为 0，再 clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`；估计器全整数运算无浮点/FPU 依赖；smoke 断言 RTO 落在合理区间并覆盖重传退避路径。
- [IRQ 上下文误用] 重传/重组/RTT 采样/唤醒若从 IRQ 触发会违反既有边界 → 缓解：TCP 输出与推进复用 ordinary-context 边界（`ordinary_context()`/`UnsupportedContext`），重传/RTT 采样/`TIME_WAIT` 推进沿用 ARP pending 式 tick 驱动的 ordinary 上下文轮询，MUST NOT 从 IRQ 分配/阻塞/操作 TCB 缓冲。
- [TCB/缓冲/重组槽无界增长] 连接与缓冲若无界会耗尽内核内存 → 缓解：连接表、发送/接收缓冲、重传队列、重组槽均编译期定容；容量满以确定性状态（`TableFull`/`QueueFull`/丢弃）处理，接收窗口用剩余空间通告有界流控，不无界缓冲。
- [标准 `2*MSL` `TIME_WAIT` 与定容 TCB 池相容性] 对齐 Linux 的长 `TIME_WAIT`（`2*MSL`）在定容 TCB 池下可能让 `TIME_WAIT` 槽占满、阻塞新连接 → 缓解：`TIME_WAIT` 语义按标准保留，槽占满时新建连接返回确定性 `TableFull`，不无界扩张、不提前破坏 `TIME_WAIT`；smoke 用较短 MSL 常量配置或直接驱动 `tcp_pump` 到期以在有界时间内验证回收，默认语义仍为标准 `2*MSL`。
- [校验和/伪首部与 loopback 归一化不一致] 本机闭环下 TCP 伪首部宿地址若与 `send_ipv4` 归一化不一致会误判 malformed → 缓解：与 `handle_udp` 一致，本机闭环统一以 `local_ipv4` 作为伪首部宿地址域，`handle_tcp` 重建校验和用同一归一化，不写特例。
- [默认启动误启用协议栈] 放宽/新增路径若被默认启动触达会影响正常 boot → 缓解：TCP 仅在显式内核内部调用或 smoke 下驱动；默认 `g_default_context` 保持 `Disabled`，normal boot 不初始化 TCP；smoke 默认关闭；默认启动回归纳入验证。

## Migration Plan

- 纯增量：新增 `kernel/core/net/tcp.cc` 与 `include/bigos/net/tcp.h`；在 `handle_ipv4` 追加协议号 6 分支调用 `handle_tcp`；在 `net.h` 追加 `IPV4_PROTOCOL_TCP=6`、TCP 相关常量与 `Diagnostics` 末尾 TCP 计数（append-only + `static_assert` 守护）；新增默认关闭 `tcp_path_smoke` 开关与 `BIGOS_TCP_PATH_SMOKE` 宏映射及 smoke 入口；`kernel/core/kernel.cc` 以 `#ifdef` 守卫 spawn smoke 线程。
- 无 ABI 迁移：不改 syscall/socket 编号与语义；不暴露用户接口；用户态无需改动。
- 回滚策略：TCP 为独立单元 + 两处挂接点（`handle_ipv4` 分支、`send_ipv4` 复用），可整体回退到「无 TCP 分支」的原行为；smoke 默认关闭不影响默认启动。
- 构建/静态检查：GCC 交叉构建默认与 smoke 两配置、clang/clangd 辅助静态检查、源级契约测试（`net.h` 常量与 `Diagnostics` 布局）、QEMU headless TCP smoke marker + 默认启动回归 marker。

## Open Questions

（以下先前问题已确认）

- RTO 策略：已确认对齐真实 Linux，实现完整 RFC 6298 动态 RTT 估计（SRTT/RTTVAR、`RTO = SRTT + max(G, K*RTTVAR)`、Karn 算法、超时指数退避），RTO clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`（对齐 Linux 200ms / 120s）。全整数/移位实现，无浮点依赖。见决策四。
- `TIME_WAIT` 策略：已确认对齐真实 Linux，采用标准 `2*MSL`（`TCP_MSL_TICKS` 常量固化 MSL，`TIME_WAIT = 2*MSL`），不因 smoke 便利缩短语义；定容 TCB 池下 `TIME_WAIT` 槽占满时新连接按 `TableFull` 处理。见决策六。
- 容量常量（`TCP_CONNECTION_CAPACITY`/`TCP_SEND_BUFFER`/`TCP_RECV_BUFFER`/`TCP_RETX_QUEUE_CAPACITY`/`TCP_REORDER_SLOTS`/`TCP_MSS`/`TCP_RTO_MIN`/`TCP_RTO_MAX`/`TCP_MSL_TICKS`/`TCP_MAX_RETRANSMIT`）：在实现阶段结合 `send_ipv4` 缓冲上界（`TCP_MSS <= UDP_MAX_PAYLOAD + 8 - 20`）、TCB 结构体大小与内核内存预算确定，均为编译期定容并 `static_assert` 守护关系。
- 被动打开（`LISTEN`）范围：已确认本变更只做协议层 `LISTEN`/`SYN_RECEIVED`/`ESTABLISHED` 迁移与内核内部 API，不碰 `accept`/不暴露任何用户接口；smoke 通过内核内部 API 验证被动打开闭环，用户接口留待后续 stream socket 能力。
