## Why

当前内核内部网络协议路径（`bigos::net`：Ethernet/ARP/IPv4/ICMP/UDP）与最小 UDP socket 接口都必须绑定到一个 ready 的帧级 `device::NetworkDevice`（virtio-net）。这带来两个问题：一是任何面向本机地址（loopback）的 datagram 流量都要经过 ARP 解析与真实/仿真网卡的 TX/RX，而本机地址本不该依赖二层解析或物理链路；二是可复现的默认关闭验证目前只能靠 `FakeDevice` + `inject_frame` 手工构造回环帧，缺少一条稳定的、面向本机地址的内核内网络路径。

面向连接的网络栈（TCP client/server、`connect` 到 loopback 地址）需要一个前置能力：一条 loopback 网络路径，使面向连接与 datagram 的本机地址流量无需物理或仿真网卡即可闭环工作，为后续 TCP 与 stream socket 提供确定性、可复现、默认关闭的验证基座。这正是本机地址网络能力的第一块拼图，需先于 TCP 路径落地。

## What Changes

- 新增一条内核内部 loopback 网络路径：把发往本机 IPv4 地址（配置的 `local_ipv4` 与回环地址 `127.0.0.1`）的 IPv4 报文（承载 ICMP/UDP，后续可承载 TCP）在协议层直接闭环回本机输入路径，无需 ARP 解析、无需帧级 `NetworkDevice`、无需真实或仿真网卡。
- loopback 路径独立于 virtio-net 帧级设备：即使没有 ready 的 `NetworkDevice`（`State::SkippedNoDevice`），本机地址的 datagram/面向连接流量也 MUST 能闭环；已有的对外（非本机）路径保持经由 `route_destination` + ARP + 帧级设备发送，行为不变。
- 明确本机地址识别与投递语义：目的地址等于本机配置地址或回环地址时走 loopback 输入；协议层据此把报文直接交给对应 UDP endpoint 的有界 RX 队列（复用现有 `UdpEndpoint`/`rx_wait`/就绪模型），而不是构造 Ethernet 帧下发设备。
- 边界与容量沿用现有有界原则：loopback 投递复用现有 UDP endpoint 定容 RX 队列与丢弃/队列满/未绑定的确定性状态，不引入无界缓冲、不新增用户可见 syscall/fd 语义、不改变既有对外网络行为与既有 socket ABI。
- 新增一个默认关闭的运行期 smoke 开关与 COM1 标记，在无 tap/无网卡环境下验证：本机 UDP send-to `127.0.0.1`/`local_ipv4` 能被本机 recvfrom 收到；面向本机地址流量在 `NetworkDevice` 缺失时仍闭环；非本机地址仍走既有对外路径；本机地址的容量满/未绑定/非法目的确定性行为；默认启动不依赖 loopback。

## Capabilities

### New Capabilities
- `loopback-network-path`: 内核内部 loopback 网络路径。识别发往本机配置地址与回环地址 `127.0.0.1` 的 IPv4 流量并在协议层直接闭环回本机输入路径，无需 ARP 解析与帧级网卡，即使无 ready `NetworkDevice` 也能工作；面向连接与 datagram 的本机地址流量复用现有 UDP endpoint 有界 RX 队列与就绪模型闭环，支撑可复现、默认关闭的验证。该能力保持有界：不实现通用 routing/多网卡/多接口地址模型，不引入新的用户可见 syscall/fd 语义，不声称完整 loopback 接口（无 `lo` 设备节点、无接口枚举）。

### Modified Capabilities
- `bounded-network-protocol-path`: 扩展 IPv4 输出与投递语义，使目的地址为本机配置地址或回环地址 `127.0.0.1` 时走 loopback 输入路径（协议层直接闭环、无 ARP、无帧级设备），其余目的地址保持既有 `route_destination` + ARP + 帧级设备发送；扩展初始化/禁用语义，使 loopback 本机地址路径在无 ready `NetworkDevice` 时仍可用，同时保持对外协议路径、IRQ 边界、malformed 丢弃与既有诊断行为不变。

## Impact

- 受影响内核子系统：内核内部网络协议路径（`kernel/core/net/protocol.cc` 的 `send_ipv4`/`route_destination`/IPv4 输入分发新增本机地址识别与 loopback 投递分支）、网络协议头（`include/bigos/net.h` 可能新增本机地址判定/回环常量与 loopback 诊断计数字段）、UDP endpoint 投递（复用现有 `udp_receive_from`/`rx_wait`/`UDP_RX_QUEUE_CAPACITY`，仅新增“协议层直接入队本机 datagram”的内部投递入口）。
- 复用而非改动既有原语：UDP endpoint 定容 RX 队列与就绪唤醒（`UdpEndpoint`/`rx_wait`/统一就绪模型）、`inject_frame`/`pump` 现有输入流水、`Status` 枚举与确定性诊断、用户 UDP socket 的 `sendto`/`recvfrom` 边界；本机地址闭环是对现有输入/投递路径的追加式内部分支，成功路径的数据搬运与唤醒逻辑不受影响。
- ABI/接口影响：不新增、不改动任何 syscall 编号、寄存器传参顺序、`int 0x80` 返回语义或用户可见 socket ABI；用户程序仅通过既有 UDP socket `sendto`/`recvfrom` 面向本机地址收发即可受益，无需新用户接口。
- 构建/验证：新增一个默认关闭 xmake smoke 开关（映射到 `BIGOS_*` 宏）与对应 COM1 标记，遵循既有 `network_protocol_smoke`/`socket_smoke` 模式，通过 QEMU headless 路径验证；不改动默认启动行为；内核内部闭环验证不依赖 tap/真实网卡。
- 非目标：不实现通用 IP routing/转发、多网卡或多接口地址模型；不实现完整 loopback 接口对象（无 `lo` 设备节点、无接口枚举/配置 ABI）；不在本变更内实现 TCP/面向连接状态机（仅提供本机地址闭环基座，供后续 TCP 路径复用）；不改变既有对外（非本机）网络行为、ARP 语义、帧级设备 TX/RX ownership 或 IRQ 边界；不引入无界缓冲或用户可见 async I/O；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 交叉工具链与 xmake；QEMU headless 为主要 smoke 路径；loopback 内核内部闭环不依赖 tap 权限或真实/仿真网卡；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
