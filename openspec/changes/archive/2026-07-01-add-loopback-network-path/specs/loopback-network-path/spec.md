## ADDED Requirements

### Requirement: 本机地址识别与 loopback 分流

BigOS SHALL 在内核内部网络协议路径的 IPv4 输出层识别“本机地址”目的，并将发往本机地址的 IPv4 报文（承载 ICMP 或 UDP）直接闭环回本机 IPv4 输入分发路径，而不构造 Ethernet 帧、不进行 ARP 解析、不调用帧级网络设备发送。本机地址集合 MUST 包含配置的本机 IPv4 地址与回环地址 `127.0.0.1`（回环网段 `127.0.0.0/8`）。分流判定 MUST 只读且无副作用，MUST NOT 改变对外（非本机）目的的既有 `route_destination` + ARP + 帧级设备发送行为。

#### Scenario: 发往本机 IPv4 地址走 loopback 输入

- **WHEN** 内核内部协议路径需要输出一个目的地址等于配置本机 IPv4 地址的 IPv4 报文（ICMP 或 UDP）
- **THEN** BigOS MUST 把该报文直接交给本机 IPv4 输入分发路径处理，而不经 ARP 解析或帧级设备发送
- **AND** 后续 ICMP/UDP 处理 MUST 复用既有输入分发（协议号分发、头部/长度/校验和校验），不旁路第二套校验

#### Scenario: 发往回环地址走 loopback 输入

- **WHEN** 内核内部协议路径需要输出一个目的地址属于回环网段 `127.0.0.0/8` 的 IPv4 报文（含 `127.0.0.1` 以及该网段内非 `.1` 的任意地址）
- **THEN** BigOS MUST 以本机闭环语义处理该报文并交给本机 IPv4 输入分发路径
- **AND** 该报文的本机闭环源/宿地址域 MUST 自洽，使既有 UDP/IPv4 校验和逻辑无需特例即可通过

#### Scenario: 非本机目的保持对外路径不变

- **WHEN** 内核内部协议路径需要输出一个目的地址既非配置本机地址也不属于回环网段的 IPv4 报文
- **THEN** BigOS MUST 保持既有 `route_destination` + ARP 解析 + 帧级设备发送路径与语义不变
- **AND** ARP 未解析、无路由、too-large、超时、设备发送失败等结果 MUST 保持既有确定性状态映射

### Requirement: 本机地址流量无需帧级网络设备即可闭环

BigOS SHALL 使本机地址 loopback 路径在没有 ready 帧级 `NetworkDevice` 时仍可工作。只要 context 完成有效本机 IPv4 配置，发往本机地址的 datagram 与面向连接流量 MUST 能通过 loopback 输入路径闭环；对外（非本机）发送 MUST 仍要求 ready 的帧级设备并在其缺失时返回确定性 not-ready/设备状态。loopback 闭环 MUST 在 ordinary（可阻塞、非 IRQ）内核上下文执行，MUST NOT 从 IRQ 上下文触发本机投递或唤醒。

#### Scenario: 无网络设备时本机 UDP 闭环

- **WHEN** context 仅配置了本机 IPv4/回环信息、没有 ready 的帧级 `NetworkDevice`，且一个本机 UDP 端口已绑定
- **THEN** 面向本机地址的 UDP 发送 MUST 通过 loopback 输入路径投递到该端口的有界 RX 队列
- **AND** 对应的本机接收操作 MUST 能取回相同 payload 与来源地址，MUST NOT 因缺少帧级设备而失败

#### Scenario: 无网络设备时对外发送确定性失败

- **WHEN** 没有 ready 帧级 `NetworkDevice`，而协议路径尝试向非本机目的地址发送
- **THEN** BigOS MUST 返回确定性 not-ready 或设备状态，MUST NOT 误报发送成功
- **AND** 本机地址 loopback 路径 MUST 不受该对外失败影响，保持独立可用

#### Scenario: loopback 投递拒绝 IRQ 上下文

- **WHEN** 从 IRQ 上下文尝试触发本机地址 loopback 投递、输入分发或端口队列唤醒
- **THEN** BigOS MUST 按既有 ordinary-context 边界拒绝或记录确定性诊断
- **AND** it MUST NOT 从 IRQ 上下文分配内存、阻塞或操作端点队列

### Requirement: loopback 投递复用有界 UDP 端点队列与就绪模型

BigOS SHALL 使本机地址 UDP loopback 投递复用既有 UDP 端点定容 RX 队列、就绪唤醒与确定性丢弃语义，MUST NOT 引入无界缓冲或第二套投递逻辑。loopback 投递 MUST 在数据完全入队后再唤醒等待者，队列满、目标端口未绑定或 payload 超限时 MUST 返回与对外 RX 一致的确定性状态。

#### Scenario: loopback datagram 入队并唤醒等待者

- **WHEN** 一个发往已绑定本机端口的 loopback UDP datagram 通过输入分发到达
- **THEN** BigOS MUST 在该端点的定容 RX 队列中入队来源地址、来源端口、payload 长度与 payload 字节，然后唤醒该端点的就绪等待队列
- **AND** 被唤醒后重新检查队列的等待者 MUST 能观察到新入队的数据

#### Scenario: 本机端口未绑定或队列满

- **WHEN** loopback UDP datagram 的目标本机端口未绑定，或目标端点 RX 队列已满
- **THEN** BigOS MUST 以确定性状态丢弃该 datagram（未绑定与队列满各自确定性状态）
- **AND** it MUST NOT 分配无界内存、覆盖无关的已入队 datagram 或对外泄漏该报文

### Requirement: 本机 ICMP echo-to-self 闭环

BigOS SHALL 支持以本机地址（配置本机 IPv4 或回环网段 `127.0.0.0/8`）为目的的 ICMPv4 echo request 经 loopback 路径闭环：echo request 经 IPv4 输出层分流进入本机输入分发，由既有 ICMP 处理生成一次 echo reply，reply 再经 loopback 分流进入本机输入分发并被识别为 echo reply（非 request），从而不再生成新的 echo request。该闭环的递归深度 MUST 有界（≤1 次回声），MUST 复用既有 ICMP 校验（校验和、payload 边界、type/code）而不旁路第二套校验。

#### Scenario: 本机 echo request 生成一次 echo reply

- **WHEN** 内核内部路径以本机地址为目的发起一个合法的 ICMPv4 echo request
- **THEN** BigOS MUST 经 loopback 输入分发处理该 echo request，并通过既有 ICMP 处理生成一个带匹配 identifier、sequence 与 payload 的 echo reply
- **AND** echo request 与 echo reply MUST 经协议层处理产生确定性诊断计数递增，且 MUST NOT 经帧级设备发送

#### Scenario: echo reply 不触发新的 request

- **WHEN** 本机 echo reply 经 loopback 分流再次进入本机输入分发
- **THEN** BigOS MUST 将其识别为 echo reply 而非 echo request，MUST NOT 生成新的 echo request
- **AND** 本机 ICMP echo 闭环的递归深度 MUST 保持有界（≤1 次回声），MUST NOT 无限递归或无界重入

### Requirement: loopback 诊断计数

BigOS SHALL 提供确定性诊断计数以区分“经 loopback 本机闭环命中”与“经对外帧级设备路径命中”，至少包含 loopback 成功交付计数与 loopback 丢弃计数。新增计数字段 MUST 以 append-only 方式加入诊断结构，MUST NOT 改变既有诊断字段的取值语义。

#### Scenario: loopback 成功交付计数递增

- **WHEN** 一个本机地址报文经 loopback 输入分发成功交付（UDP 入队成功或 ICMP echo 处理成功）
- **THEN** BigOS MUST 递增 loopback 成功交付计数
- **AND** 既有 `udp_received`/`ipv4_rx`/`icmp_echo_*` 等计数 MUST 保持其原有语义并按既有规则并行递增

#### Scenario: loopback 丢弃计数递增

- **WHEN** 一个本机地址报文经 loopback 路径因目标端口未绑定、RX 队列已满或校验失败被丢弃
- **THEN** BigOS MUST 递增 loopback 丢弃计数并保持既有确定性丢弃状态
- **AND** it MUST NOT 将 loopback 丢弃计为成功交付

### Requirement: loopback 能力边界

BigOS SHALL 将 loopback 网络路径限定为本机地址的协议层闭环基座，MUST NOT 引入通用 IP routing/转发、多网卡或多接口地址模型、`lo` 设备节点、接口枚举/配置 ABI，或新的用户可见 syscall/fd 语义。用户程序 MUST 仅通过既有 UDP socket 收发面向本机地址即可受益。

#### Scenario: 不新增用户可见接口

- **WHEN** loopback 网络路径被编译进内核或验证被启用
- **THEN** 既有 syscall 编号、fd 行为、UDP socket ABI、VFS 挂载与 userland 程序 MUST 保持不变
- **AND** 用户程序 MUST NOT 从本变更获得新的网络设备节点、接口配置或 routing 接口

#### Scenario: 不实现通用 routing

- **WHEN** 需要区分本机地址与对外地址
- **THEN** BigOS MUST 仅按本机地址集合（本机 IPv4 与回环网段）与既有 `route_destination` 边界判定
- **AND** it MUST NOT 引入通用路由表、转发、多接口地址选择或动态路由

### Requirement: loopback 默认关闭验证与默认启动独立性

BigOS SHALL 通过默认关闭 smoke 验证 loopback 网络路径，覆盖无设备下本机 UDP 闭环成功、本机 ICMP echo-to-self 闭环、非本机目的确定性失败、端口未绑定/队列满/非法目的确定性行为，并保证默认启动在未启用验证、无网络配置时不依赖 loopback。验证 MUST 提供无需真实 tap/网卡即可运行的内核内部闭环路径；依赖不可用时 MUST 记录跳过原因与剩余风险。

#### Scenario: loopback smoke 闭环通过

- **WHEN** 启用 loopback 验证 build switch 并在受控环境运行 smoke（仅本机配置、无帧级设备）
- **THEN** smoke MUST 覆盖 bind、面向 `127.0.0.1`/本机地址的 send、本机 receive 闭环与错误路径（未绑定、队列满、非本机目的），并发出确定性通过/失败 marker
- **AND** 成功 MUST 依赖协议层本机闭环与确定性端点状态（含 loopback 交付计数），而非帧级设备 TX/RX

#### Scenario: smoke 覆盖本机 ICMP echo-to-self

- **WHEN** loopback smoke 以本机地址为目的发起一个 ICMPv4 echo request
- **THEN** smoke MUST 断言经 loopback 路径产生一次 echo reply（`icmp_echo_requests`/`icmp_echo_replies` 与 loopback 交付计数确定性递增），且未经帧级设备发送
- **AND** smoke MUST 断言该闭环递归深度有界（echo reply 不触发新的 echo request）

#### Scenario: 默认启动不依赖 loopback

- **WHEN** loopback 验证 switch 关闭或无网络配置
- **THEN** 默认启动、storage、filesystem、`/rw`、shell 与 userland baseline MUST 保持与 loopback 能力无关并正常进入 shell
- **AND** 缺少 loopback 初始化 MUST NOT 阻止正常启动验证运行

#### Scenario: 验证不可用时记录跳过

- **WHEN** QEMU、串口捕获或 x86_64-elf 工具链等验证依赖不可用
- **THEN** 验证记录 MUST 区分已通过、无法运行（含原因与剩余风险）与历史诊断
- **AND** it MUST NOT 声称运行成功
