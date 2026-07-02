## ADDED Requirements

### Requirement: DNS client 复用既有 UDP socket 语义
BigOS DNS client behavior SHALL be implemented as a userland consumer of the existing bounded UDP socket interface. DNS resolution MUST create or use UDP datagram sockets through the documented socket/bind/sendto/recvfrom wrappers and MUST NOT require new socket domains, socket types, socket options, ancillary data, scatter-gather operations, or a DNS-specific syscall. DNS usage MUST preserve existing datagram socket fd lifecycle and deterministic error mapping.

#### Scenario: DNS query 使用 UDP sendto
- **WHEN** the DNS client sends a DNS query to a configured DNS server
- **THEN** it MUST use the existing bounded UDP socket send path with an IPv4 `sockaddr_in` destination on port 53
- **AND** it MUST NOT bypass the documented socket fd, user-buffer, or errno translation paths

#### Scenario: DNS response 使用 UDP recvfrom
- **WHEN** the DNS client waits for a DNS response
- **THEN** it MUST use the existing bounded UDP socket receive path and validate the returned source/address and payload before parsing DNS data
- **AND** any no-data or would-block result MUST map to the resolver's bounded timeout/no-response behavior

#### Scenario: DNS 不扩大 socket API
- **WHEN** DNS client support is built or validation is enabled
- **THEN** existing socket domain/type/protocol support, datagram `read`/`write` unsupported behavior, stream socket behavior, and syscall numbers MUST remain unchanged
- **AND** DNS support MUST NOT add general resolver state to socket fd objects
