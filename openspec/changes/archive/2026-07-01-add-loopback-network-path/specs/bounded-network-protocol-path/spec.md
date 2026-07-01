## MODIFIED Requirements

### Requirement: 网络协议路径初始化与静态配置
BigOS SHALL provide a kernel-internal bounded network protocol path that can bind to one ready frame-level network device, initialize local IPv4/MAC/MTU state from boot-time kernel options, and remain disabled when no suitable configuration is available. Initialization MUST run in ordinary kernel context and MUST NOT create user-visible socket, fd, syscall, device-node, mount, or network-configuration ABI. The path SHALL additionally support a loopback-only readiness mode: when valid local IPv4 configuration is present but no ready frame-level network device is available, the kernel-internal local-address (loopback) delivery path MUST remain usable while outbound (non-local) transmission remains gated on a ready frame-level device.

#### Scenario: 使用已发布网络设备和 boot-time 配置初始化
- **WHEN** a ready kernel-internal network device is published and valid boot-time IPv4 configuration is provided
- **THEN** BigOS MUST initialize the bounded protocol context with deterministic local MAC, IPv4 address, netmask or direct-peer boundary, MTU, and diagnostics state
- **AND** it MUST expose only kernel-internal protocol operations to subsequent kernel consumers or validation paths

#### Scenario: 无设备但有本机配置时 loopback 就绪
- **WHEN** valid local IPv4 configuration is present but no ready frame-level network device is published
- **THEN** BigOS MUST keep the kernel-internal local-address (loopback) delivery path usable for local-address traffic
- **AND** outbound (non-local) transmission MUST return deterministic not-ready or device status without claiming success

#### Scenario: 缺少配置时保持禁用
- **WHEN** no ready network device is published and required local IPv4 configuration is missing or invalid
- **THEN** BigOS MUST leave the protocol path disabled with deterministic diagnostics
- **AND** default boot, storage, filesystem, shell, and userland baseline behavior MUST remain independent of protocol initialization

#### Scenario: 不创建用户态网络 ABI
- **WHEN** the bounded network protocol path is compiled in or validation is enabled
- **THEN** existing syscall numbers, fd behavior, VFS mounts, userland programs, and libc interfaces MUST remain unchanged
- **AND** user programs MUST NOT gain socket, network device, or network configuration access from this change alone

### Requirement: IPv4 输入输出最小路径
BigOS SHALL implement a bounded IPv4 path for unfragmented packets carrying ICMP or UDP, including header length validation, total length validation, checksum validation, local-address filtering, TTL handling for output, and deterministic rejection of unsupported protocols or fragments. IPv4 output SHALL classify the destination address: destinations equal to the configured local IPv4 address or within the loopback network `127.0.0.0/8` MUST be delivered through the kernel-internal local-address (loopback) input dispatch path without ARP resolution or frame-level device transmission, while all other destinations MUST use the existing `route_destination` + ARP + frame-level device path. Local-address delivery MUST reuse the existing IPv4 input dispatch (protocol demultiplex, header/length/checksum validation) and MUST NOT introduce a second validation path.

#### Scenario: 接受有效本机 IPv4 包
- **WHEN** an Ethernet IPv4 payload contains a valid IPv4 header, checksum, total length, no fragmentation, and a destination matching the configured local IPv4 address, an accepted loopback address, or an accepted broadcast boundary
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

#### Scenario: 本机地址目的走 loopback 输入
- **WHEN** ICMP or UDP output requests an IPv4 packet whose destination is the configured local IPv4 address or a loopback address in `127.0.0.0/8`
- **THEN** BigOS MUST deliver the constructed IPv4 packet directly to the local-address input dispatch path without ARP resolution or frame-level device transmission
- **AND** the loopback source/destination address domain MUST be self-consistent so existing UDP/IPv4 checksum logic passes without a special case

#### Scenario: 构造 IPv4 输出（对外目的）
- **WHEN** ICMP or UDP handling requests an IPv4 packet to a non-local destination that is a configured peer or ARP-resolved destination
- **THEN** BigOS MUST construct an IPv4 header with valid total length, checksum, TTL, protocol number, source address, and destination address and transmit it through the frame-level device path
- **AND** the packet MUST remain within MTU after Ethernet encapsulation or fail with deterministic too-large status
