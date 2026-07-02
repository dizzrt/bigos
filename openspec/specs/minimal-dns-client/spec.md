# minimal-dns-client Specification

## Purpose
TBD - created by archiving change add-minimal-dns-client. Update Purpose after archive.
## Requirements
### Requirement: 最小 DNS 查询构造
BigOS SHALL provide a freestanding userland DNS client that constructs one bounded UDP DNS query for one hostname at a time. The query MUST use QTYPE=A and QCLASS=IN, carry exactly one question, encode labels with deterministic length checks, and reject malformed hostnames before sending. DNS message, hostname, label, and output capacities MUST be compile-time bounded.

#### Scenario: 构造有效 A 查询
- **WHEN** 用户程序请求解析一个由普通 DNS labels 组成且位于 BigOS 长度上界内的 hostname
- **THEN** DNS client MUST construct one DNS query with one question, QTYPE=A, QCLASS=IN, and a transaction ID tracked for the response
- **AND** it MUST send the query as one UDP datagram to the configured DNS server port 53 through the existing UDP socket path

#### Scenario: 拒绝非法 hostname
- **WHEN** hostname 为空、label 为空、label 超过单 label 上界、总长度超过 hostname 上界，或包含 BigOS 不接受的字符形式
- **THEN** DNS client MUST fail before sending any UDP datagram
- **AND** it MUST return the documented failure sentinel and set a deterministic errno

### Requirement: DNS 响应解析与匹配
BigOS SHALL parse bounded DNS UDP responses for the outstanding query and extract IPv4 A records. Response parsing MUST validate message length, transaction ID, QR bit, opcode, rcode, question match, RR bounds, RDATA length, and name compression pointers. Unsupported RR types MAY be skipped within bounds, but MUST NOT be exposed as IPv4 results. Compression pointer parsing MUST detect loops and out-of-message offsets.

#### Scenario: 解析匹配的 A 记录响应
- **WHEN** a UDP response arrives from the DNS server with a matching transaction ID, a successful DNS response code, the expected question, and one or more A/IN answers
- **THEN** DNS client MUST copy up to the caller-provided output capacity of IPv4 addresses in host-order BigOS IPv4 representation
- **AND** it MUST return the number of copied IPv4 addresses

#### Scenario: 拒绝不匹配或 malformed 响应
- **WHEN** a DNS response has a mismatched transaction ID, malformed header, unsupported opcode, nonzero failure rcode, mismatched question, truncated field, invalid RDATA length, or invalid compression pointer
- **THEN** DNS client MUST reject that response with deterministic failure
- **AND** it MUST NOT read beyond the received UDP payload or write partial unvalidated addresses as successful results

#### Scenario: 无 A 记录返回确定性失败
- **WHEN** a syntactically valid response matches the query but contains no usable A/IN answers
- **THEN** DNS client MUST return a deterministic no-result failure
- **AND** it MUST NOT fabricate an IPv4 address from unsupported RR data

### Requirement: 用户态 resolver API
BigOS SHALL expose a freestanding-safe userland API for resolving one hostname to one or more IPv4 addresses. The API MUST require explicit caller-provided output storage and capacity, MUST accept an explicit DNS server IPv4 address or a documented bounded userland configuration helper, and MUST return success/failure through libc errno conventions. BigOS SHALL additionally expose a minimal `netdb.h` compatibility entry for this bounded IPv4 A-record resolver subset. The API and `netdb.h` entry MUST NOT require hosted libc, threads, dynamic initialization, heap allocation, locale, or a general resolver database.

#### Scenario: API 成功返回 IPv4 地址数量
- **WHEN** a simple static BigOS C program calls the resolver API with a valid hostname, DNS server IPv4, output buffer, capacity, and timeout bound, and the DNS server returns at least one valid A record
- **THEN** the API MUST return a positive address count and populate the output array with host-order IPv4 addresses
- **AND** the caller MUST be able to use one returned address with the existing `sockaddr_in` socket address layout

#### Scenario: netdb.h 最小兼容入口可用
- **WHEN** a simple static BigOS C program includes `netdb.h` and calls the documented minimal compatibility resolver entry for an IPv4 A-record lookup
- **THEN** the header and implementation MUST compile and link in the freestanding userland build
- **AND** the exposed behavior MUST remain limited to the BigOS bounded IPv4 A-record resolver subset

#### Scenario: API 拒绝容量不足或非法参数
- **WHEN** the caller passes a null hostname, null output pointer, zero output capacity, invalid DNS server IPv4, or an output capacity smaller than required for the chosen API contract
- **THEN** the API MUST fail with `errno` set to a deterministic value
- **AND** it MUST NOT send DNS traffic using invalid caller storage

### Requirement: 有界等待与错误映射
BigOS SHALL make DNS resolution use bounded UDP send/receive behavior. The resolver MUST close any socket fd it creates on all success and failure paths, MUST map UDP/socket failures through existing libc errno conventions, and MUST map DNS-specific failures to deterministic errno values without adding a DNS syscall error domain.

#### Scenario: 超时或无响应
- **WHEN** no matching DNS response is received within the resolver's bounded wait or retry policy
- **THEN** DNS resolution MUST fail with `errno` set to `ETIMEDOUT`
- **AND** it MUST release any created UDP socket fd before returning

#### Scenario: UDP socket 发送或接收失败
- **WHEN** the existing UDP socket path returns no route, not ready, queue full, would-block, malformed address, or another deterministic socket error
- **THEN** DNS resolver MUST propagate or map that failure through the documented libc errno convention
- **AND** it MUST NOT report a successful DNS result

### Requirement: 默认关闭 DNS 验证
BigOS SHALL validate the minimal DNS client through default-off validation that does not require a real external DNS server for baseline coverage. Validation MUST cover successful A-record resolution through a controlled UDP response path, malformed response rejection, transaction ID mismatch, no A records, truncation/TC handling, timeout/no response, and default boot independence.

#### Scenario: loopback DNS smoke 成功
- **WHEN** DNS validation is enabled in a controlled environment with a loopback or injected UDP DNS responder
- **THEN** BigOS MUST resolve a known test hostname to a deterministic IPv4 address through the userland DNS client
- **AND** success MUST require DNS wire-format query construction and response parsing, not direct test-only address injection into the resolver result

#### Scenario: DNS 错误路径 smoke
- **WHEN** DNS validation injects malformed, mismatched, truncated, no-answer, or timeout cases
- **THEN** DNS client MUST return deterministic failures for each exercised category
- **AND** it MUST NOT leak fd resources or claim success for skipped network prerequisites

#### Scenario: 默认启动不依赖 DNS
- **WHEN** DNS validation is disabled, no DNS server is configured, or no network backend is available
- **THEN** default boot, storage, filesystem, shell, and existing userland baseline behavior MUST remain independent of DNS resolution
- **AND** absence of DNS configuration MUST NOT prevent normal boot validation from reaching the shell

