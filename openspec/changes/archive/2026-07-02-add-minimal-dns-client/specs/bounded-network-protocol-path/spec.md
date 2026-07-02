## ADDED Requirements

### Requirement: DNS 流量保持为 UDP 上层 payload
BigOS SHALL treat DNS traffic as ordinary UDP payload carried by the existing bounded IPv4/UDP protocol path. DNS support MUST NOT add DNS parsing to the kernel protocol demultiplexer, MUST NOT alter ARP/IPv4/UDP checksum or routing behavior, and MUST NOT move protocol work into network-device IRQ context. The existing network protocol path MUST continue to expose only bounded packet transport and diagnostics below the socket layer.

#### Scenario: DNS datagram 经既有 IPv4/UDP 路径传输
- **WHEN** a DNS client sends or receives a DNS UDP datagram
- **THEN** the datagram MUST pass through the existing UDP, IPv4, loopback/outbound routing, ARP, and frame-device boundaries according to the destination address
- **AND** DNS support MUST NOT introduce a second IPv4 output path or a special DNS demultiplexing path in the kernel

#### Scenario: DNS 不改变 IRQ 边界
- **WHEN** a network device reports RX/TX completion while DNS validation or resolution is active
- **THEN** IRQ handlers MUST remain limited to device completion state as required by the existing network protocol path
- **AND** DNS parsing MUST occur only in userland resolver code after a UDP payload has been received through the socket API

#### Scenario: 默认无 DNS 配置时协议路径仍独立
- **WHEN** no DNS server is configured or DNS validation is disabled
- **THEN** the bounded network protocol path MUST retain its existing initialization, loopback, UDP, TCP, diagnostics, and default-off validation behavior
- **AND** absence of DNS configuration MUST NOT change protocol readiness or default boot behavior
