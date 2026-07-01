## Context

内核内部网络协议路径 `bigos::net`（`kernel/core/net/protocol.cc`）已实现 Ethernet/ARP/IPv4/ICMP/UDP 的有界处理，并由最小 UDP socket 接口（`kernel/core/net/socket.cc`）暴露给用户态。当前输出路径固定为：

```
udp_send_to -> send_ipv4 -> route_destination -> arp_resolve -> transmit_ethernet -> device->transmit
```

而输入路径为：

```
device->poll_rx (或 inject_frame) -> pump -> handle_ethernet -> handle_ipv4 -> handle_udp -> endpoint->rx_queue -> wake_all(rx_wait)
```

两条路径都以帧级 `device::NetworkDevice`（virtio-net）为中心：

- 初始化 `init()` 要求一个 ready 的 `NetworkDevice`，否则进入 `State::SkippedNoDevice` 并整体禁用协议路径。
- 任何输出（含发往本机地址）都要经过 `route_destination` + `arp_resolve`。发往本机 IPv4 的报文当前没有 ARP 表项、也没有对端会应答，必然 `ArpUnresolved`。
- 唯一能触发本机输入的手段是测试代码里的 `FakeDevice` + `inject_frame` 手工构造回环帧。

面向连接网络栈需要“连接到 loopback 地址”这一前置能力：本机地址流量应当无需二层解析、无需真实/仿真网卡即可闭环。因此需要在协议层引入一条明确的 loopback 路径，并让本机地址路径不再强依赖 ready 的帧级设备。

约束：freestanding-safe、x86_64-only、无 hosted libc；协议处理 MUST 保持在 ordinary（非 IRQ）内核上下文；容量与缓冲保持有界；不改动既有 syscall/socket ABI；不改动 boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## Goals / Non-Goals

**Goals:**

- 在协议层识别“本机地址”目的（配置 `local_ipv4` 与回环地址 `127.0.0.1`），并把发往本机地址的 IPv4 报文（承载 ICMP/UDP）直接闭环回本机输入路径，不经 ARP、不经帧级设备。
- 使本机地址 loopback 路径在无 ready `NetworkDevice` 时仍可用，供无 tap/无网卡环境下可复现、默认关闭的验证使用。
- 复用现有 IPv4 输入分发（`handle_ipv4` → `handle_udp`/`handle_icmp`）、UDP endpoint 定容 RX 队列与 `rx_wait` 就绪唤醒，保证用户 `sendto` 本机地址后 `recvfrom` 能收到，且就绪模型一致。
- 保持对外（非本机）路径完全不变：`route_destination` + ARP + 帧级设备发送、TX/RX ownership、IRQ 边界、malformed 丢弃与既有诊断行为。
- 提供一个默认关闭 smoke（映射 `BIGOS_*` 宏 + COM1 标记），覆盖本机地址闭环成功、无设备下闭环、非本机仍走对外、容量满/未绑定/非法确定性行为、默认启动独立性。

**Non-Goals:**

- 不实现通用 IP routing/转发、多网卡/多接口地址模型或路由表。
- 不实现完整 loopback 接口对象：无 `lo` 设备节点、无接口枚举/配置 syscall/ABI。
- 不在本变更内实现 TCP/面向连接状态机；仅提供本机地址闭环基座，供后续 TCP 路径复用（TCP 亦通过“本机地址走 loopback 输入”的同一分发点闭环）。
- 不新增或改动任何用户可见 syscall 编号、寄存器传参顺序、`int 0x80` 返回语义或 UDP socket ABI。
- 不引入无界缓冲或用户可见 async I/O；不改动 boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## Decisions

### 决策一：在 IPv4 输出层（`send_ipv4`）做 loopback 分流，而非 UDP 层或设备层

在 `send_ipv4` 构造好完整 IPv4 报文后、进入 `route_destination`/`arp_resolve` 之前，判定目的地址是否为“本机地址”。若是，则不调用 `transmit_ethernet`，而是把刚构造的 IPv4 报文直接交给输入分发 `handle_ipv4(__ctx, packet, total_len)`（内部再按协议号分发到 `handle_udp`/`handle_icmp`）。

数据流对比：

```
对外目的:  send_ipv4 -> route_destination -> arp_resolve -> transmit_ethernet -> device->transmit
本机目的:  send_ipv4 -> (识别本机地址) -> handle_ipv4 -> handle_udp/handle_icmp -> rx_queue/echo reply
```

- 选择 IPv4 输出层的原因：ICMP echo 与 UDP 都经由 `send_ipv4` 输出，单一分流点即可同时让 UDP loopback 与 ICMP echo-to-self 闭环，并天然复用 `handle_ipv4` 的头部/校验/分片/协议号校验，避免旁路校验产生第二套判定。
- 备选（UDP 层直接入队）被否：只覆盖 UDP、不覆盖 ICMP，且需在 `udp_send_to` 里复制一份“找 endpoint、查队列容量、入队、唤醒”的逻辑，与 `handle_udp` 漂移风险高。
- 备选（loopback 伪设备 `NetworkDevice`，把帧塞回 `poll_rx`）被否：需要构造完整 Ethernet 帧并伪造 MAC/ARP，重量级且引入不必要的二层语义；与“本机地址不该依赖二层”的目标相悖。

### 决策二：本机地址判定集中为一个内部谓词

新增内部判定 `is_local_delivery(ctx, dest)`：`dest == ctx->config.local_ipv4` 或 `dest ∈ 127.0.0.0/8`。回环判定按整个 `127.0.0.0/8` 网段（判定 `(dest & 0xff000000) == 0x7f000000`），而非仅精确匹配 `127.0.0.1`，以匹配习惯用法。在 `include/bigos/net.h` 增补回环地址常量（如 `IPV4_LOOPBACK = 127.0.0.1`）与回环网段掩码/前缀常量（`127.0.0.0/8`）。判定只读、无副作用，供 `send_ipv4` 分流使用。

- `handle_ipv4` 现有的本机过滤 `dest == local_ipv4 || dest == BROADCAST` 需要放宽：当报文来自 loopback 分流（源亦为本机/回环）时，目的可能是 `127.0.0.1` 而非 `local_ipv4`，需被接受。实现上让 `handle_ipv4` 接受本机地址集合（`local_ipv4` 或回环网段），保持广播语义不变；非本机目的仍按 `ipv4_not_local` 丢弃。

### 决策三：源地址与校验和处理

loopback UDP 报文的源地址取本机地址：UDP send 到 `127.0.0.1` 时，输入侧 `recvfrom` 报告的 source IPv4 取分流时使用的源（`ctx->config.local_ipv4` 或 `127.0.0.1`，与目的域一致，保证 UDP 伪首部校验和自洽）。`udp_send_to` 已按 `local_ipv4` 作为源构造伪首部校验和；`handle_udp` 用 `__source` 与 `local_ipv4` 重建伪首部校验。为使 loopback 校验自洽，分流时传入 `handle_ipv4` 的报文源地址与 `handle_udp` 重建所用 `local_ipv4` 必须一致。设计上：本机地址闭环统一以 `local_ipv4` 作为源/宿域（`127.0.0.1` 视为 `local_ipv4` 的别名进入输入路径），从而复用既有校验和逻辑无需特例。

### 决策四：初始化与禁用语义放宽

`init()` 在无 ready `NetworkDevice` 时当前进入 `State::SkippedNoDevice` 并整体禁用。调整为：即使无帧级设备，只要有有效 boot-time IPv4/local 配置（或使用默认本机/回环配置），loopback 本机地址路径 MUST 可用。

- 引入独立于 `device != nullptr` 的“loopback 就绪”判定：`send_ipv4` 的对外分支仍要求 `device` 存在且 ready（否则返回既有 `NotReady`/设备状态），而 loopback 分支只要求 context 完成本机地址配置。
- 保持默认启动独立性：未启用验证、无配置时协议路径整体保持禁用，默认 boot/storage/fs/shell/userland 行为不受影响。

### 决策五：诊断计数区分 loopback 与对外命中

在 `Diagnostics` 末尾追加 `loopback_delivered`（本机地址报文成功交付本机输入路径的次数）与 `loopback_dropped`（本机地址报文因未绑定/队列满/校验失败等在 loopback 路径被丢弃的次数）两个计数字段。新增字段一律末尾追加并以 `static_assert` 守护既有字段偏移不变。

- 增补原因：smoke 与后续验证需要确定性地区分“报文经 loopback 本机闭环命中”与“经对外帧级设备路径命中”，仅靠既有 `udp_received`/`udp_queue_full` 无法区分两条路径的来源。显式 loopback 计数使验证不必依赖设备侧计数间接推断。
- 与既有计数关系：`loopback_delivered` 与既有 `udp_received`/`ipv4_rx` 并行递增（loopback 报文仍走 `handle_ipv4`/`handle_udp`），loopback 计数只标注“该次命中来自本机闭环分流”。不改动既有计数语义。

### 决策六：验证以内核内部闭环为主，含 UDP 与 ICMP echo-to-self

新增默认关闭 smoke（`--loopback_network_smoke`，映射 `BIGOS_LOOPBACK_NETWORK_SMOKE` → `BIGOS_LOOPBACK_NETWORK_PASSED/FAILED`），在无 `NetworkDevice`、无 tap 的条件下覆盖两类本机闭环：

- UDP 本机闭环（主验证目标）：初始化仅带本机配置的 context；bind 一个本机 UDP 端口；`udp_send_to(127.0.0.1/local_ipv4, port)`；`udp_receive_from` 校验收到相同 payload 与 source；`loopback_delivered` 计数递增；覆盖非本机目的仍走对外分支（无设备时确定性失败而非误报成功）、端口未绑定/队列满/非法目的的确定性状态。
- ICMP echo-to-self（纳入本变更验证）：以本机地址（`127.0.0.1` 或 `local_ipv4`）为目的通过内核内部路径发起一次 ICMPv4 echo request，经 `send_ipv4` loopback 分流进入 `handle_ipv4`/`handle_icmp`，`handle_icmp` 生成 echo reply 再经 loopback 分流回到 `handle_ipv4` 被识别为 echo reply（不再生成新 request，递归深度 ≤1）。smoke 断言 echo request 与 echo reply 的诊断计数（`icmp_echo_requests`/`icmp_echo_replies`）确定性递增、`loopback_delivered` 递增，且不误走对外帧级设备发送。

两类闭环均通过 QEMU headless 路径验证并发出确定性 COM1 标记。

## Risks / Trade-offs

- [本机过滤放宽引入越权接受风险] `handle_ipv4` 接受本机地址集合放宽后，可能误接受非预期目的 → 缓解：仅接受精确 `local_ipv4`、回环网段与既有广播；对外 RX 帧的本机过滤逻辑保持等价，非本机一律 `ipv4_not_local` 丢弃并计数。
- [源/宿地址域不一致导致 UDP 校验失败] loopback 分流若源地址与 `handle_udp` 重建伪首部所用 `local_ipv4` 不一致会误判 malformed → 缓解：统一以 `local_ipv4` 作为本机闭环源/宿域，`127.0.0.1` 归一化为 `local_ipv4` 进入输入路径，复用既有校验和逻辑，不写特例校验。
- [无设备下误启用整套协议栈] 放宽初始化可能让本无网络配置的默认启动意外进入 Ready → 缓解：loopback 就绪仍要求显式本机配置或 smoke 配置；默认无配置时整体保持禁用；smoke 默认关闭；默认 boot 回归纳入验证。
- [IRQ 上下文误用] loopback 分流复用 `handle_ipv4`/`handle_udp`/`wake_all`，这些必须在 ordinary 上下文运行 → 缓解：`udp_send_to`/`send_ipv4` 已限定 ordinary（可阻塞）上下文；保持既有 `ordinary_context()`/`UnsupportedContext` 边界，不从 IRQ 触发 loopback 投递。
- [递归/重入] `send_ipv4` 调用 `handle_ipv4`，后者对 loopback 报文不会再产生新的对外 `send_ipv4`（UDP 入队即止；ICMP echo reply 目的为原源=本机，会再次走 loopback，形成一次有界回声而非无限递归，因为 echo reply 不再触发新的 echo request）→ 缓解：分流仅处理 send 侧发起的报文；`handle_udp` 入队即终止；`handle_icmp` 仅对 echo request 生成一次 reply，reply 为 echo reply 类型不再被 `handle_icmp` 当作 request 处理，递归深度有界（≤1）。设计中显式记录该有界深度。
- [容量语义] loopback 复用 UDP endpoint 定容 RX 队列，队列满时返回 `QueueFull`，与对外 RX 一致，不引入无界缓冲。

## Migration Plan

- 纯增量：新增内部谓词与 `send_ipv4` 分流分支、放宽 `handle_ipv4` 本机过滤与 `init` loopback 就绪判定、`net.h` 增补回环常量与 `loopback_delivered`/`loopback_dropped` 诊断计数、新增默认关闭 smoke 与 xmake 开关。
- 无 ABI 迁移：不改 syscall/socket 编号与语义；用户态无需改动，通过既有 UDP socket 面向 `127.0.0.1` 即受益。
- 回滚策略：分流分支与放宽判定均为局部改动，可整体回退到“本机地址走 ARP/设备”的原行为；smoke 默认关闭不影响默认启动。

## Open Questions

（无未决问题；以下先前问题已确认）

- 回环判定粒度：已确认按整个 `127.0.0.0/8` 网段判定（非仅 `127.0.0.1`），以匹配习惯用法，并在 spec 场景固化。
- 诊断计数：已确认在 `Diagnostics` 末尾追加 `loopback_delivered`/`loopback_dropped`，以便 smoke 与后续验证确定性区分 loopback 与对外路径命中。
- ICMP echo-to-self：已确认纳入本变更验证，与 UDP 本机闭环并列为 smoke 覆盖目标（UDP 为主，ICMP echo-to-self 一并断言诊断计数）。
