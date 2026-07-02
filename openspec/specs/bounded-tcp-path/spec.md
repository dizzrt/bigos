## Purpose

定义 BigOS 有界内核内部 TCP 协议路径：在已有静态 IPv4/loopback 网络协议路径之上提供定容
TCP 连接控制块、显式 TCP 状态机、三次握手连接建立、有序数据交付与有界乱序重组、RFC 6298
动态重传与窗口流控、标准 `TIME_WAIT` 连接拆除、确定性 TCP 诊断计数，以及默认关闭验证。该
能力复用既有 IPv4 输入分发与输出层（含本机地址 loopback 分流），限定为内核内部有界路径，
不实现完整 TCP 特性矩阵，不引入新的用户可见 socket、fd、syscall 或名字解析接口。

## Requirements

### Requirement: TCP 段输入分发与输出承载

BigOS SHALL 在内核内部网络协议路径中新增有界 TCP 段处理：既有 IPv4 输入分发 MUST 在协议号为 6（TCP）时把 TCP 段交给 TCP 处理入口，既有 IPv4 输出 MUST 能承载 TCP 段。TCP 段的输出 MUST 复用既有 IPv4 输出层（含本机地址 loopback 分流与对外 `route_destination` + ARP + 帧级设备发送），本机地址目的的 TCP 段 MUST 经既有 loopback 闭环无需 ARP/帧级设备。TCP 段校验和 MUST 使用 IPv4 伪首部（源/宿 IPv4、协议号 6、TCP 长度）+ TCP 头 + 数据，且本机地址闭环时伪首部宿地址域 MUST 与既有本机地址归一化一致，使重建校验和无需特例。

#### Scenario: IPv4 输入按协议号分发 TCP 段

- **WHEN** 一个通过既有 IPv4 头部/长度/分片/校验和校验的 IPv4 包携带协议号 6（TCP）且目的为本机地址集合
- **THEN** BigOS MUST 把 TCP 段交给 TCP 处理入口，并保持既有 ICMP/UDP 分发与 malformed 丢弃语义不变
- **AND** TCP 处理 MUST 在既有校验通过的 IPv4 载荷边界内读取 TCP 段，MUST NOT 越过校验后的 IPv4 总长度

#### Scenario: 本机地址 TCP 段经 loopback 闭环

- **WHEN** TCP 状态机需要输出一个目的为配置本机 IPv4 地址或回环网段 `127.0.0.0/8` 的 TCP 段
- **THEN** BigOS MUST 经既有 IPv4 输出层把该段直接交给本机地址输入分发闭环，MUST NOT 进行 ARP 解析或帧级设备发送
- **AND** TCP 伪首部校验和的宿地址域 MUST 与本机地址归一化一致，使 TCP 处理入口重建校验和无需特例即可通过

#### Scenario: 对外 TCP 段保持既有输出路径

- **WHEN** TCP 状态机需要输出一个目的既非本机地址也不属于回环网段的 TCP 段
- **THEN** BigOS MUST 复用既有 `route_destination` + ARP + 帧级设备发送路径，并在无 ready 帧级设备时返回确定性 not-ready/设备状态
- **AND** MUST NOT 为 TCP 另建第二套 IPv4 输出或校验和逻辑

### Requirement: 定容连接控制块与 TCP 状态机

BigOS SHALL 提供定容 TCP 连接控制块（TCB）池与显式 TCP 状态机，覆盖 `CLOSED`、`LISTEN`、`SYN_SENT`、`SYN_RECEIVED`、`ESTABLISHED`、`FIN_WAIT_1`、`FIN_WAIT_2`、`CLOSING`、`CLOSE_WAIT`、`LAST_ACK`、`TIME_WAIT` 状态迁移。连接查找 MUST 按四元组（本地 IPv4/port、远端 IPv4/port）精确匹配，被动打开的 `LISTEN` MUST 按本地端口匹配。序号比较 MUST 采用 32 位回绕安全比较。连接表、发送/接收缓冲、重传队列与乱序重组槽 MUST 为编译期定容上界，MUST NOT 无界增长。

#### Scenario: 分配与查找连接

- **WHEN** TCP 处理需要为一个新四元组建立连接或为到达的 TCP 段查找已有连接
- **THEN** BigOS MUST 在定容 TCB 池中按四元组精确匹配已有连接，或在有空闲槽时分配一个新 TCB
- **AND** 被动打开时 MUST 按本地端口匹配处于 `LISTEN` 的 TCB

#### Scenario: 连接表满

- **WHEN** 需要新建连接但定容 TCB 池已无空闲槽
- **THEN** BigOS MUST 以确定性状态拒绝新建连接，MUST NOT 无界扩张连接表或覆盖无关的活动连接
- **AND** MUST 递增确定性 TCP 丢弃/表满诊断计数

#### Scenario: 序号回绕安全比较

- **WHEN** TCP 需要比较到达段的序号/确认号与连接期望的 `rcv_nxt`/`snd_una`/`snd_nxt`
- **THEN** BigOS MUST 使用 32 位回绕安全比较判定段落在窗口内、重复或超前
- **AND** MUST NOT 因序号回绕而误判有效段为越界或误判越界段为有效

### Requirement: TCP 连接建立

BigOS SHALL 实现有界三次握手连接建立，覆盖主动打开（发送 SYN → `SYN_SENT` → 收 SYN,ACK → 发 ACK → `ESTABLISHED`）与被动打开。被动打开 MUST 采用 Linux/BSD 风格 listener 与子连接分离的半连接(SYN)+全连接(accept)双队列模型：一个处于 `LISTEN` 的本地端口收到匹配 SYN 时，MUST 派生一个独立的子连接 TCB（`LISTEN` → 派生子 TCB 并登记半连接队列 → 发 SYN,ACK → 子 TCB 进入 `SYN_RECEIVED` → 收 ACK → 子 TCB 进入 `ESTABLISHED` 并移入全连接队列），而 listener TCB MUST 保持 `LISTEN` 以继续接受后续入站连接。连接建立 MUST 在 ordinary（可阻塞、非 IRQ）内核上下文推进，MUST 正确初始化双向序号空间，并对非法或不匹配的握手段以确定性状态处理。子连接 TCB MUST 从既有定容 TCB 池分配；当无空闲槽、半连接队列已满或全连接队列已满时，新入站连接 MUST 以确定性状态拒绝/丢弃，MUST NOT 无界扩张或复用 listener 自身作为单一连接。

#### Scenario: 主动打开完成三次握手

- **WHEN** 内核内部路径对一个本机地址目的发起主动打开
- **THEN** BigOS MUST 发送 SYN 进入 `SYN_SENT`，在收到匹配的 SYN,ACK 后发送 ACK 并进入 `ESTABLISHED`
- **AND** MUST 以对端初始序号初始化 `rcv_nxt` 并推进本地 `snd_nxt`/`snd_una`，递增确定性连接建立诊断计数

#### Scenario: 被动打开派生子连接完成三次握手

- **WHEN** 一个处于 `LISTEN` 的本地端口收到匹配的 SYN
- **THEN** BigOS MUST 派生一个独立的子连接 TCB、对其发送 SYN,ACK 进入 `SYN_RECEIVED`，在收到匹配 ACK 后使该子连接进入 `ESTABLISHED`，同时 listener MUST 保持 `LISTEN`
- **AND** MUST 正确初始化子连接双向序号空间并递增确定性连接建立诊断计数

#### Scenario: listener 接受多个连接

- **WHEN** 一个处于 `LISTEN` 的本地端口先后收到来自不同四元组的多个匹配 SYN，且定容 TCB 池与半/全连接队列有可用容量
- **THEN** BigOS MUST 为每个入站连接派生独立子连接 TCB 并分别完成握手，listener MUST 始终保持 `LISTEN`
- **AND** 各已完成子连接 MUST 移入 listener 名下的全连接队列供上层取用

#### Scenario: 拒绝非法或不匹配握手段

- **WHEN** 到达的握手段序号/确认号与连接状态不匹配、标志组合非法或校验和失败
- **THEN** BigOS MUST 以确定性状态丢弃或复位（视状态发送 RST），MUST NOT 迁移到 `ESTABLISHED`
- **AND** MUST 递增确定性 TCP 丢弃/复位诊断计数

#### Scenario: 子连接资源或队列耗尽

- **WHEN** listener 收到匹配 SYN 但定容 TCB 池无空闲槽或 listener 的已完成连接队列已满
- **THEN** BigOS MUST 以确定性状态拒绝/丢弃该入站连接并递增确定性 TCP 丢弃诊断计数
- **AND** MUST NOT 无界扩张连接表/队列，MUST NOT 复用 listener 自身作为该连接

### Requirement: 有序数据交付与有界重组

BigOS SHALL 以序号驱动实现有序数据交付：落在接收窗口内且按序（序号等于 `rcv_nxt`）的数据 MUST 进入定容接收缓冲并推进 `rcv_nxt`；乱序但落在接收窗口内的段 MUST 在定容乱序重组槽内有界缓存，缺口填补后按序合并推进；重复、超出接收窗口、越界或校验失败的段 MUST 以确定性状态丢弃并计数。累积 ACK MUST 推进发送侧 `snd_una` 并回收重传队列中已确认的段。接收缓冲与重组槽满时 MUST 停止推进窗口（有界流控），MUST NOT 无界缓冲。

#### Scenario: 按序数据交付

- **WHEN** `ESTABLISHED` 连接收到序号等于 `rcv_nxt` 且落在接收窗口内的数据段
- **THEN** BigOS MUST 把数据写入定容接收缓冲、推进 `rcv_nxt`、并回送累积 ACK
- **AND** 内核内部接收操作 MUST 能按序读到该数据

#### Scenario: 乱序段有界重组

- **WHEN** 连接收到序号高于 `rcv_nxt` 但落在接收窗口内的乱序数据段
- **THEN** BigOS MUST 在定容乱序重组槽内有界缓存该段，并在缺口被按序段填补后按序合并推进 `rcv_nxt`
- **AND** 重组槽已满时 MUST 以确定性状态丢弃超额乱序段，MUST NOT 无界缓冲

#### Scenario: 丢弃重复或超窗段

- **WHEN** 连接收到序号低于 `rcv_nxt`（重复）、超出接收窗口、越界或校验和失败的段
- **THEN** BigOS MUST 以确定性状态丢弃该段并递增确定性 TCP 丢弃诊断计数
- **AND** MUST NOT 写入未校验数据或破坏已交付的有序数据

#### Scenario: 累积 ACK 推进发送侧

- **WHEN** 连接收到确认号推进的累积 ACK
- **THEN** BigOS MUST 推进 `snd_una` 并从定容重传队列回收已被确认的段与其重传计时
- **AND** MUST NOT 把过期或重复 ACK 当作新确认破坏发送窗口

### Requirement: RFC 6298 动态重传与窗口流控

BigOS SHALL 按 RFC 6298 实现动态重传超时：每个未确认段 MUST 登记进定容重传队列并记录发送 tick；TCP MUST 维护平滑往返时间 `SRTT` 与往返时间方差 `RTTVAR`，按 `RTO = SRTT + max(G, K*RTTVAR)`（K=4，G 为时钟粒度）计算重传超时，并将 `RTO` clamp 到编译期下界 `TCP_RTO_MIN` 与上界 `TCP_RTO_MAX`。RTT 采样 MUST 遵循 Karn 算法（重传过的段不用于 RTT 采样）。到期未确认段 MUST 被重传，且重传时 MUST 按指数退避（`RTO = min(RTO*2, TCP_RTO_MAX)`）加倍；重传次数超过编译期上界 `TCP_MAX_RETRANSMIT` MUST 以确定性状态复位/放弃连接。SRTT/RTTVAR/RTO 计算 MUST 使用整数/移位运算，MUST NOT 依赖浮点。重传、RTT 采样与超时推进 MUST 在 ordinary 内核上下文进行（沿用协议路径的非 IRQ 超时推进模型），MUST NOT 从 IRQ 上下文触发重传、RTT 采样、分配或缓冲操作。TCP MUST 通过接收缓冲剩余空间通告有界接收窗口做流控。

#### Scenario: RFC 6298 RTT 采样更新 RTO

- **WHEN** 一个未被重传过的段被累积 ACK 确认，产生一次有效 RTT 测量
- **THEN** BigOS MUST 按 RFC 6298 更新 `SRTT`/`RTTVAR`（首次采样 `SRTT=R`、`RTTVAR=R/2`，后续 `RTTVAR=(1-1/4)*RTTVAR+1/4*|SRTT-R'|`、`SRTT=(1-1/8)*SRTT+1/8*R'`）并重算 `RTO = SRTT + max(G, K*RTTVAR)`
- **AND** 计算结果 MUST clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`，MUST 使用整数运算且 MUST NOT 依赖浮点

#### Scenario: Karn 算法不对重传段采样

- **WHEN** 一个被重传过至少一次的段随后被确认
- **THEN** BigOS MUST NOT 用该段的往返时间更新 `SRTT`/`RTTVAR`（避免重传二义性）
- **AND** RTO 的收敛 MUST 仅依赖未重传段的有效 RTT 采样

#### Scenario: 到期段重传并指数退避

- **WHEN** 一个登记在重传队列中的未确认段的重传超时在 ordinary 上下文推进中到期
- **THEN** BigOS MUST 重传该段并按 `RTO = min(RTO*2, TCP_RTO_MAX)` 指数退避更新其重传计时与重传计数
- **AND** MUST 递增确定性 TCP 重传诊断计数，退避 MUST NOT 改写 `SRTT`/`RTTVAR`

#### Scenario: 重传超限复位连接

- **WHEN** 某段的重传次数超过编译期最大重传上界 `TCP_MAX_RETRANSMIT`
- **THEN** BigOS MUST 以确定性状态复位或放弃该连接并回收其 TCB
- **AND** MUST 递增确定性 TCP 复位诊断计数，MUST NOT 无限重传

#### Scenario: 通告窗口流控

- **WHEN** 接收缓冲剩余空间变化或已满
- **THEN** BigOS MUST 通过通告窗口向对端反映有界可接收空间，接收缓冲满时 MUST 通告有界（含 0）窗口
- **AND** MUST NOT 无界缓冲或接受超过通告窗口的数据

#### Scenario: 重传推进不在 IRQ 上下文

- **WHEN** 从 IRQ 上下文尝试触发 TCP 重传、RTT 采样、超时推进或缓冲操作
- **THEN** BigOS MUST 按既有 ordinary-context 边界拒绝或记录确定性诊断
- **AND** MUST NOT 从 IRQ 上下文分配内存、阻塞或操作 TCB 缓冲

### Requirement: 连接拆除与标准 TIME_WAIT 回收

BigOS SHALL 实现连接拆除：正常关闭 MUST 发送 FIN 并按对端 ACK/FIN 完成 `FIN_WAIT_1`/`FIN_WAIT_2`/`TIME_WAIT` 迁移；被动关闭收到 FIN MUST 进入 `CLOSE_WAIT`，本地关闭后 `LAST_ACK`；同时关闭 MUST 走 `CLOSING`。`TIME_WAIT` MUST 使用标准 `2*MSL` 超时（以编译期常量 `TCP_MSL_TICKS` 表示 MSL，`TIME_WAIT` 时长 = `2*TCP_MSL_TICKS`），到期 MUST 回收 TCB。收到 RST、重传超限或非法段 MUST 以确定性状态复位并回收 TCB。回收 MUST 释放该连接占用的定容缓冲与重组槽而不泄漏。由于 TCB 池定容，当 `TIME_WAIT` 槽占满时新建连接 MUST 返回确定性表满状态，MUST NOT 提前破坏 `TIME_WAIT` 语义或无界扩张。

#### Scenario: 正常 FIN 双向拆除

- **WHEN** 一个 `ESTABLISHED` 连接发起正常关闭
- **THEN** BigOS MUST 发送 FIN 进入 `FIN_WAIT_1`，按对端 ACK/FIN 迁移到 `FIN_WAIT_2`/`TIME_WAIT`
- **AND** MUST 递增确定性连接关闭诊断计数

#### Scenario: 被动关闭与同时关闭

- **WHEN** 连接在 `ESTABLISHED` 先收到对端 FIN（被动关闭），或双方近似同时发送 FIN（同时关闭）
- **THEN** BigOS MUST 分别经 `CLOSE_WAIT`→`LAST_ACK` 或 `CLOSING` 完成拆除
- **AND** MUST 在拆除完成后回收 TCB 与其定容缓冲/重组槽

#### Scenario: 标准 2*MSL TIME_WAIT 回收

- **WHEN** 连接进入 `TIME_WAIT` 且标准 `2*MSL`（`2*TCP_MSL_TICKS`）超时在 ordinary 上下文推进中到期
- **THEN** BigOS MUST 回收该 TCB 使其槽位可被后续连接复用
- **AND** `TIME_WAIT` 时长 MUST 遵循标准 `2*MSL` 语义，MUST NOT 为验证便利而缩短默认语义

#### Scenario: TIME_WAIT 槽占满时表满

- **WHEN** 定容 TCB 池的 `TIME_WAIT` 槽占满而需要新建连接
- **THEN** BigOS MUST 返回确定性表满状态并递增确定性丢弃/表满诊断计数
- **AND** MUST NOT 提前回收处于 `TIME_WAIT` 的 TCB 或无界扩张连接表

#### Scenario: RST 或异常复位

- **WHEN** 连接收到匹配的 RST、重传超限或不可恢复的非法段
- **THEN** BigOS MUST 以确定性状态复位连接并立即回收 TCB
- **AND** MUST 递增确定性 TCP 复位诊断计数，MUST NOT 泄漏定容缓冲

### Requirement: TCP 诊断计数

BigOS SHALL 提供确定性 TCP 诊断计数以区分 TCP 段的接收、发送、重传、连接建立/关闭、复位与确定性丢弃，至少包含 TCP 段接收数、TCP 段发送数、重传数、连接建立数、连接关闭数、复位数与丢弃数。新增计数字段 MUST 以 append-only 方式加入既有诊断结构末尾，MUST NOT 改变既有诊断字段的取值语义或偏移。

#### Scenario: TCP 计数确定性递增

- **WHEN** TCP 处理接收/发送段、重传、建立或关闭连接、复位或按确定性状态丢弃段
- **THEN** BigOS MUST 递增对应的确定性 TCP 诊断计数
- **AND** 既有 IPv4/ICMP/UDP/loopback 计数 MUST 保持其原有语义与偏移不变

#### Scenario: append-only 布局守护

- **WHEN** 编译内核并运行诊断结构的源级布局校验
- **THEN** 新增 TCP 计数字段 MUST 位于既有诊断字段之后
- **AND** 既有字段偏移 MUST 由编译期断言守护不变

### Requirement: TCP 能力边界

BigOS SHALL 将本能力限定为内核内部有界 TCP 协议路径，MUST NOT 实现完整 TCP 特性矩阵，MUST NOT 暴露新的用户可见 syscall/fd/socket 语义或名字解析。具体地，本能力 MUST NOT 实现拥塞控制算法、慢启动/拥塞避免、SACK、窗口缩放、时间戳/PAWS、urgent 指针、Nagle/delayed-ACK 调优矩阵或 keepalive 特性；MUST NOT 暴露 `connect`/`listen`/`accept` 用户接口；MUST NOT 实现通用 IP routing/转发或多接口地址模型。RTO/RTT 估计按 RFC 6298 实现（属本能力目标，不在排除之列）。

#### Scenario: 不新增用户可见接口

- **WHEN** 有界 TCP 路径被编译进内核或验证被启用
- **THEN** 既有 syscall 编号、fd 行为、UDP socket ABI、VFS 挂载与 userland 程序 MUST 保持不变
- **AND** 用户程序 MUST NOT 从本变更获得 stream socket 接口、名字解析或新的网络配置接口

#### Scenario: 不实现完整 TCP 特性矩阵

- **WHEN** 连接建立、数据传输或重传需要处理特性协商
- **THEN** BigOS MUST 实现连接建立、RFC 6298 动态重传、有序交付与连接拆除
- **AND** MUST NOT 引入拥塞控制、SACK、窗口缩放、时间戳/PAWS、urgent 指针或 keepalive 特性

### Requirement: TCP 默认关闭验证与默认启动独立性

BigOS SHALL 通过默认关闭 smoke 验证有界 TCP 路径，覆盖本机地址连接建立、`ESTABLISHED` 有序双向数据交付、乱序/重复段确定性处理、RFC 6298 重传路径（含 RTT 采样与指数退避）、正常/被动/同时关闭与标准 `TIME_WAIT` 回收、RST/重传超限确定性复位、连接表满与非法段确定性行为，并保证默认启动在未启用验证、无网络配置时不依赖 TCP。验证 MUST 提供无需真实 tap/网卡即可运行的内核内部本机地址闭环路径（基于 loopback 就绪）；依赖不可用时 MUST 记录跳过原因与剩余风险。

#### Scenario: TCP smoke 闭环通过

- **WHEN** 启用 TCP 验证 build switch 并在受控环境（仅本机配置、无帧级设备的 loopback 就绪）运行 smoke
- **THEN** smoke MUST 覆盖本机地址三次握手、有序双向数据交付、重传路径与正常/异常连接拆除，并发出确定性通过/失败 marker
- **AND** 成功 MUST 依赖协议层 TCP 状态机与确定性 TCP 诊断计数，而非帧级设备 TX/RX

#### Scenario: TCP smoke 覆盖错误与边界路径

- **WHEN** smoke 注入乱序段、重复段、非法握手段、触发重传超限、填满连接表或触发 RST
- **THEN** BigOS MUST 为每个被触发条件记录确定性丢弃/复位/表满类别
- **AND** MUST NOT 把不同的协议、重传、容量与复位结果合并为泛化成功

#### Scenario: 默认启动不依赖 TCP

- **WHEN** TCP 验证 switch 关闭或无网络配置
- **THEN** 默认启动、storage、filesystem、`/rw`、shell 与 userland baseline MUST 保持与 TCP 能力无关并正常进入 shell
- **AND** 缺少 TCP 初始化 MUST NOT 阻止正常启动验证运行

#### Scenario: 验证不可用时记录跳过

- **WHEN** QEMU、串口捕获或 x86_64-elf 工具链等验证依赖不可用
- **THEN** 验证记录 MUST 区分已通过、无法运行（含原因与剩余风险）与历史诊断
- **AND** MUST NOT 声称运行成功

### Requirement: 半连接/全连接双队列与连接级就绪等待

BigOS SHALL 为被动打开提供 listener 名下的两条定容队列：半连接队列（SYN queue，容纳 `SYN_RECEIVED` 握手中的子连接）与全连接队列（accept queue，容纳已完成三次握手 `ESTABLISHED` 但尚未被上层取走的子连接），并提供内核内部入口从全连接队列取出一个已完成连接。两队列容量 MUST 为编译期定容上界。定容 TCB 池容量 MUST 足以在保持 listener 的同时容纳半连接、全连接与主动连接的并发占用（含本机地址闭环下 listener+client+child 同池占用），并以编译期断言固化该容量不变式。BigOS SHALL 为 TCB 提供连接级等待队列，使数据到达、连接进入 `ESTABLISHED`、有新入站连接可取、或连接进入错误/复位状态时唤醒等待者，供上层 fd readiness 与有界阻塞路径复用。所有取连接、入队与唤醒 MUST 在 ordinary 内核上下文进行，MUST NOT 从 IRQ 上下文操作队列或等待队列。

#### Scenario: 从 listener 取出一个已完成连接

- **WHEN** 内核内部入口请求从一个 listener 的全连接队列取出连接且队列非空
- **THEN** BigOS MUST 返回一个 `ESTABLISHED` 子连接 TCB 并将其从全连接队列移除
- **AND** 队列为空时 MUST 返回确定性无连接状态，MUST NOT 阻塞在 IRQ 上下文或忙等

#### Scenario: 连接级等待队列唤醒

- **WHEN** 一个等待者登记在某连接（或 listener）的连接级等待队列上，随后该连接收到按序数据、进入 `ESTABLISHED`、有新入站连接完成、或被复位
- **THEN** BigOS MUST 通过该等待队列唤醒等待者
- **AND** 唤醒 MUST 发生在 ordinary 上下文，MUST NOT 从 IRQ 上下文触发连接缓冲操作

#### Scenario: 半连接/全连接队列有界

- **WHEN** 握手中的子连接达到半连接队列上界，或已完成连接持续入队而上层未及时取走达到全连接队列上界
- **THEN** BigOS MUST 以确定性状态处理超额入站连接/握手，MUST NOT 无界扩张任一队列

#### Scenario: TCB 池容量不变式

- **WHEN** 编译内核并运行 TCB 池与队列容量的源级断言
- **THEN** 定容 TCB 池容量 MUST 满足「listener + 半连接队列 + 全连接队列 + 主动连接预算」的不变式
- **AND** 该不变式 MUST 由编译期断言守护，MUST NOT 因保持 listener 而使单条本机连接建立即耗尽连接池
