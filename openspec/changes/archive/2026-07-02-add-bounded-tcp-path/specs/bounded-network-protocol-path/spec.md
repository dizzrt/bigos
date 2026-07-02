## MODIFIED Requirements

### Requirement: IPv4 输入输出最小路径
BigOS SHALL implement a bounded IPv4 path for unfragmented packets carrying ICMP, UDP, or TCP, including header length validation, total length validation, checksum validation, local-address filtering, TTL handling for output, and deterministic rejection of unsupported protocols or fragments. IPv4 input dispatch MUST demultiplex by protocol number and hand ICMP (1), UDP (17), and TCP (6) payloads to their corresponding bounded handlers while preserving existing malformed-drop behavior for unsupported protocol numbers. IPv4 output SHALL classify the destination address: destinations equal to the configured local IPv4 address or within the loopback network `127.0.0.0/8` MUST be delivered through the kernel-internal local-address (loopback) input dispatch path without ARP resolution or frame-level device transmission, while all other destinations MUST use the existing `route_destination` + ARP + frame-level device path. IPv4 output MUST be able to carry TCP segments in addition to ICMP and UDP payloads, reusing the same loopback-split and outbound paths. Local-address delivery MUST reuse the existing IPv4 input dispatch (protocol demultiplex, header/length/checksum validation) and MUST NOT introduce a second validation path.

#### Scenario: 接受有效本机 IPv4 包
- **WHEN** an Ethernet IPv4 payload contains a valid IPv4 header, checksum, total length, no fragmentation, and a destination matching the configured local IPv4 address, an accepted loopback address, or an accepted broadcast boundary
- **THEN** BigOS MUST dispatch ICMP, UDP, or TCP payloads to the corresponding bounded handler by protocol number
- **AND** it MUST preserve packet bounds so handlers cannot read beyond the validated IPv4 total length

#### Scenario: 丢弃 invalid IPv4 包
- **WHEN** an IPv4 packet has an invalid version, header length, checksum, total length, destination address, or unsupported protocol number
- **THEN** BigOS MUST drop the packet with deterministic diagnostics
- **AND** it MUST NOT dispatch the payload to ICMP, UDP, TCP, or future socket-facing layers

#### Scenario: 拒绝 IPv4 分片
- **WHEN** an IPv4 packet has a nonzero fragment offset or the more-fragments flag set
- **THEN** BigOS MUST reject the packet as unsupported fragmentation with deterministic diagnostics
- **AND** it MUST NOT allocate reassembly buffers or expose partial payloads as complete datagrams

#### Scenario: 本机地址目的走 loopback 输入
- **WHEN** ICMP, UDP, or TCP output requests an IPv4 packet whose destination is the configured local IPv4 address or a loopback address in `127.0.0.0/8`
- **THEN** BigOS MUST deliver the constructed IPv4 packet directly to the local-address input dispatch path without ARP resolution or frame-level device transmission
- **AND** the loopback source/destination address domain MUST be self-consistent so existing UDP/IPv4/TCP checksum logic passes without a special case

#### Scenario: 构造 IPv4 输出（对外目的）
- **WHEN** ICMP, UDP, or TCP handling requests an IPv4 packet to a non-local destination that is a configured peer or ARP-resolved destination
- **THEN** BigOS MUST construct an IPv4 header with valid total length, checksum, TTL, protocol number, source address, and destination address and transmit it through the frame-level device path
- **AND** the packet MUST remain within MTU after Ethernet encapsulation or fail with deterministic too-large status
