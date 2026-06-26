## Purpose

定义 BigOS 有界内核内部网络协议路径：在已有帧级网络设备之上提供静态 IPv4/ARP/ICMP/UDP
处理、确定性诊断、默认关闭验证，以及明确的 IRQ 和用户态 ABI 边界。该能力不引入 socket、
fd、syscall、设备节点、挂载点或用户态网络配置接口。

## Requirements

### Requirement: 网络协议路径初始化与静态配置
BigOS SHALL provide a kernel-internal bounded network protocol path that can bind to one ready frame-level network device, initialize local IPv4/MAC/MTU state from boot-time kernel options, and remain disabled when no suitable device or configuration is available. Initialization MUST run in ordinary kernel context and MUST NOT create user-visible socket, fd, syscall, device-node, mount, or network-configuration ABI.

#### Scenario: 使用已发布网络设备和 boot-time 配置初始化
- **WHEN** a ready kernel-internal network device is published and valid boot-time IPv4 configuration is provided
- **THEN** BigOS MUST initialize the bounded protocol context with deterministic local MAC, IPv4 address, netmask or direct-peer boundary, MTU, and diagnostics state
- **AND** it MUST expose only kernel-internal protocol operations to subsequent kernel consumers or validation paths

#### Scenario: 缺少设备或配置时保持禁用
- **WHEN** no ready network device is published, the device reports not-ready state, or required boot-time IPv4 configuration is missing or invalid
- **THEN** BigOS MUST leave the protocol path disabled with deterministic diagnostics
- **AND** default boot, storage, filesystem, shell, and userland baseline behavior MUST remain independent of protocol initialization

#### Scenario: 不创建用户态网络 ABI
- **WHEN** the bounded network protocol path is compiled in or validation is enabled
- **THEN** existing syscall numbers, fd behavior, VFS mounts, userland programs, and libc interfaces MUST remain unchanged
- **AND** user programs MUST NOT gain socket, network device, or network configuration access from this change alone

### Requirement: 以太网帧分发与发送构造
BigOS SHALL parse and construct only bounded Ethernet II frames for ARP, IPv4, and explicitly diagnosed unsupported ethertypes. Frame parsing MUST validate minimum Ethernet header length, destination MAC acceptance, MTU-sized payload bounds, and device ownership handoff before exposing payloads to ARP or IPv4 handlers.

#### Scenario: 分发本机或广播帧
- **WHEN** the network device reports a received Ethernet frame addressed to the local MAC, broadcast MAC, or an accepted multicast/control address used by this bounded path
- **THEN** BigOS MUST validate the frame length and ethertype before dispatching ARP or IPv4 payloads
- **AND** it MUST return or retain the RX buffer according to a single tracked ownership state

#### Scenario: 丢弃无关或 malformed 以太网帧
- **WHEN** a received frame is shorter than an Ethernet header, exceeds the tracked MTU/buffer boundary, targets an unrelated destination MAC, or has an unsupported ethertype
- **THEN** BigOS MUST drop or reject the frame with deterministic diagnostics
- **AND** it MUST NOT pass malformed payload bytes to ARP, IPv4, ICMP, UDP, or future socket-facing layers

#### Scenario: 构造发送帧
- **WHEN** ARP, ICMP, or UDP handling requests transmission of a bounded payload to a resolved destination MAC
- **THEN** BigOS MUST construct one Ethernet II frame within MTU and network-device frame-size limits
- **AND** it MUST submit the frame through the existing kernel-internal network device TX operation and report deterministic success, timeout, no-slot, not-ready, or malformed status

### Requirement: ARP 解析与缓存
BigOS SHALL implement bounded ARP handling for IPv4 Ethernet networks, including request reply for the local address, bounded peer MAC cache, bounded pending resolution, timeout, and deterministic failure states. ARP state MUST NOT grow without a fixed capacity or explicit initialization-time capacity.

#### Scenario: 回复本机 ARP 请求
- **WHEN** a valid ARP request asks for the configured local IPv4 address on the bound Ethernet device
- **THEN** BigOS MUST send an ARP reply containing the configured local MAC and IPv4 address
- **AND** it MUST update deterministic ARP diagnostics without exposing user-visible network state

#### Scenario: 解析 UDP 或 ICMP 目的 MAC
- **WHEN** IPv4 output requires a destination MAC that is not present in the bounded ARP cache
- **THEN** BigOS MUST issue a bounded ARP request or return a deterministic unresolved/timeout status according to the configured wait policy
- **AND** it MUST NOT allocate unbounded request objects or block from IRQ context

#### Scenario: 拒绝 malformed ARP
- **WHEN** an ARP packet has unsupported hardware/protocol type, invalid length, unsupported operation, or inconsistent sender/target fields
- **THEN** BigOS MUST reject the packet with deterministic diagnostics
- **AND** it MUST NOT poison the ARP cache with unvalidated peer state

#### Scenario: ARP cache 容量耗尽
- **WHEN** the bounded ARP cache or pending-resolution table has no free slot
- **THEN** BigOS MUST return or record a deterministic cache-full status
- **AND** it MUST NOT overwrite an unrelated active entry except through a documented bounded eviction policy

### Requirement: IPv4 输入输出最小路径
BigOS SHALL implement a bounded IPv4 path for unfragmented packets carrying ICMP or UDP, including header length validation, total length validation, checksum validation, local-address filtering, TTL handling for output, and deterministic rejection of unsupported protocols or fragments.

#### Scenario: 接受有效本机 IPv4 包
- **WHEN** an Ethernet IPv4 payload contains a valid IPv4 header, checksum, total length, no fragmentation, and a destination matching the configured local IPv4 address or accepted broadcast boundary
- **THEN** BigOS MUST dispatch ICMP or UDP payloads to the corresponding bounded handler
- **AND** it MUST preserve packet bounds so handlers cannot read beyond the validated IPv4 total length

#### Scenario: 丢弃 invalid IPv4 包
- **WHEN** an IPv4 packet has an invalid version, header length, checksum, total length, destination address, or unsupported protocol number
- **THEN** BigOS MUST drop the packet with deterministic diagnostics
- **AND** it MUST NOT dispatch the payload to ICMP, UDP, or future socket-facing layers

#### Scenario: 拒绝 IPv4 分片
- **WHEN** an IPv4 packet has a nonzero fragment offset or the more-fragments flag set
- **THEN** BigOS MUST reject the packet as unsupported fragmentation with deterministic diagnostics
- **AND** it MUST NOT allocate reassembly buffers or expose partial payloads as complete datagrams

#### Scenario: 构造 IPv4 输出
- **WHEN** ICMP or UDP handling requests an IPv4 packet to a configured peer or ARP-resolved destination
- **THEN** BigOS MUST construct an IPv4 header with valid total length, checksum, TTL, protocol number, source address, and destination address
- **AND** the packet MUST remain within MTU after Ethernet encapsulation or fail with deterministic too-large status

### Requirement: ICMP echo 有界处理
BigOS SHALL implement bounded ICMPv4 echo handling sufficient for basic reachability validation. The path MUST validate ICMP checksum and payload bounds, reply to echo requests addressed to the local IPv4 address, and optionally generate echo requests for default-off validation without implementing broad ICMP error handling.

#### Scenario: 回复有效 echo request
- **WHEN** a valid ICMPv4 echo request is received through a validated local IPv4 packet
- **THEN** BigOS MUST send an ICMPv4 echo reply with matching identifier, sequence, and bounded payload bytes
- **AND** it MUST use the IPv4 and Ethernet output paths with deterministic transmit status

#### Scenario: 拒绝 invalid ICMP
- **WHEN** an ICMP packet has invalid checksum, truncated header, unsupported type/code for this bounded path, or payload length beyond the validated IPv4 boundary
- **THEN** BigOS MUST reject or drop the packet with deterministic diagnostics
- **AND** it MUST NOT generate malformed replies or read outside the packet buffer

#### Scenario: 默认关闭 reachability smoke
- **WHEN** the ICMP validation path is enabled with a configured peer and network backend
- **THEN** BigOS MUST be able to observe a deterministic echo request/reply outcome or an explicit environment skip/failure category
- **AND** success MUST require protocol-level ICMP processing, not only virtio-net frame publication

### Requirement: UDP datagram 内核内部接口
BigOS SHALL provide a kernel-internal bounded UDP datagram interface with explicit local port binding, send-to IPv4:port, receive-from IPv4:port, bounded per-port queue capacity, payload length validation, checksum handling according to IPv4 UDP rules, and deterministic timeout/no-data/queue-full/error statuses. The interface MUST NOT expose user buffers, fd objects, syscalls, POSIX socket flags, or errno mapping.

#### Scenario: 绑定 UDP 端口
- **WHEN** a kernel-internal consumer or validation path requests a UDP local port within the supported range and the bounded binding table has capacity
- **THEN** BigOS MUST bind the port to one internal endpoint and return deterministic success
- **AND** duplicate binds or table exhaustion MUST return deterministic errors without changing unrelated endpoints

#### Scenario: 发送 UDP datagram
- **WHEN** a bound UDP endpoint sends a payload within configured UDP and MTU limits to a reachable IPv4:port
- **THEN** BigOS MUST construct UDP, IPv4, and Ethernet headers with valid lengths and checksums as required
- **AND** it MUST report deterministic success, ARP unresolved, too-large, no-route, not-ready, timeout, or device-TX failure status

#### Scenario: 接收 UDP datagram
- **WHEN** a valid UDP packet addressed to a bound local port arrives through the validated IPv4 path
- **THEN** BigOS MUST enqueue the datagram in that endpoint's bounded RX queue with source IPv4, source port, payload length, and payload bytes
- **AND** a kernel-internal receive operation MUST return the datagram or a deterministic no-data/timeout status

#### Scenario: UDP 队列满或端口未绑定
- **WHEN** a valid UDP packet targets an unbound port or a bound endpoint whose RX queue is full
- **THEN** BigOS MUST drop the datagram with deterministic diagnostics
- **AND** it MUST NOT allocate unbounded memory, overwrite unrelated queued datagrams, or expose the packet to user-visible state

### Requirement: 协议处理上下文与 IRQ 边界
BigOS SHALL keep network protocol parsing, ARP resolution, IPv4/ICMP/UDP handling, and UDP endpoint queue operations outside virtio-net MSI-X IRQ handlers. IRQ handlers MUST remain limited to network-device completion state, while protocol progress MUST run in ordinary kernel context, a bounded validation pump, or an explicitly non-IRQ kernel worker path.

#### Scenario: RX 完成后由非 IRQ 协议 pump 处理
- **WHEN** the network device reports RX completion from its interrupt handler
- **THEN** the handler MUST only make the frame observable through the frame-level device state
- **AND** protocol parsing MUST occur later in a non-IRQ context that can safely run bounded parsing and transmission logic

#### Scenario: 协议路径拒绝 IRQ 上下文调用
- **WHEN** ARP resolution, IPv4 dispatch, ICMP handling, UDP bind/send/receive, or protocol pump execution is attempted from IRQ context
- **THEN** BigOS MUST reject the operation or record a deterministic diagnostic according to the operation boundary
- **AND** it MUST NOT allocate memory, block, wait for ARP resolution, or manipulate endpoint queues from IRQ context

#### Scenario: 网络设备 TX/RX ownership 保持一致
- **WHEN** protocol handling consumes a received frame and optionally sends one or more bounded response frames
- **THEN** BigOS MUST return the consumed RX frame to the network device exactly once after protocol processing is complete
- **AND** each TX result MUST be tracked without converting stale, timed-out, or unrelated device completions into protocol success

### Requirement: 协议验证可复现且默认关闭
BigOS SHALL provide default-off validation for the bounded network protocol path that covers initialization, ARP request/reply, IPv4 validation, ICMP echo, UDP send/receive, unsupported packet rejection, capacity failure, and default boot regression when the required toolchain and emulator/network backend are available. Validation MUST record skipped coverage and residual risk when prerequisites are unavailable.

#### Scenario: smoke 覆盖协议成功路径
- **WHEN** the expected cross toolchain, build switch, boot-time IPv4 options, serial capture, QEMU modern virtio-net backend, reused virtio-net host helper, protocol packet injection, tap setup, and required permissions are available
- **THEN** validation MUST exercise ARP resolution or reply, IPv4 validation, ICMP echo, and UDP datagram send/receive through the bounded protocol path
- **AND** success MUST depend on protocol-level parsing and deterministic endpoint state, not only on network-device frame TX/RX

#### Scenario: smoke 覆盖错误路径
- **WHEN** validation injects malformed Ethernet, ARP, IPv4, ICMP, or UDP input, unsupported ethertype/protocol, ARP cache exhaustion, UDP queue full, or missing-device conditions
- **THEN** BigOS MUST record deterministic failure or drop categories for each exercised condition
- **AND** it MUST NOT collapse distinct protocol, device, timeout, capacity, and environment-skip results into a generic success

#### Scenario: 默认启动不依赖协议网络
- **WHEN** protocol validation is disabled, no network backend is attached, or no static IPv4 configuration is provided
- **THEN** BigOS MUST preserve existing default boot, storage, filesystem, `/rw`, shell, and userland baseline behavior
- **AND** absence of network protocol initialization MUST NOT prevent normal boot validation from running

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, tap setup, host permissions, serial capture, modern virtio-net, MSI-X delivery, reused virtio-net host helper, protocol packet injection, Bochs default-boot validation, cross binutils, or required ROM/display dependencies are unavailable
- **THEN** validation records MUST identify skipped coverage and remaining risk
- **AND** they MUST NOT claim runtime network protocol smoke success for a skipped environment
