## ADDED Requirements

### Requirement: libc 暴露有界 DNS 名字解析接口
BigOS 用户态 libc SHALL expose a freestanding-safe minimal DNS name-resolution interface for simple static C programs, including a BigOS-specific resolver declaration and a minimal `netdb.h` compatibility entry. The interface MUST be limited to resolving one hostname to bounded IPv4 A-record results, MUST use caller-provided output storage, and MUST follow existing libc errno conventions. The interface MUST NOT claim complete POSIX `getaddrinfo`, `gethostbyname`, resolver state, `/etc/resolv.conf`, locale, thread-safety, or hosted libc support unless a later spec adds those behaviors.

#### Scenario: 简单 C 程序包含 DNS 解析头
- **WHEN** a BigOS simple static C program includes the documented DNS resolver header
- **THEN** the program MUST see only freestanding-safe declarations, constants, and types required for the bounded IPv4 resolver API
- **AND** those declarations MUST NOT depend on host libc, threads, dynamic initialization, locale, or unsupported POSIX resolver structs

#### Scenario: 简单 C 程序包含 netdb.h
- **WHEN** a BigOS simple static C program includes `netdb.h`
- **THEN** the program MUST see the documented minimal compatibility declaration for bounded IPv4 A-record lookup
- **AND** `netdb.h` MUST NOT declare unsupported POSIX resolver databases, service lookup APIs, thread-safe variants, or full `getaddrinfo` behavior

#### Scenario: DNS resolver 成功遵循 libc 返回约定
- **WHEN** a user program calls the DNS resolver API and resolution succeeds
- **THEN** the libc function MUST return the documented nonnegative success value and populate caller-provided IPv4 output storage
- **AND** it MUST NOT require the caller to interpret kernel-internal negative errno values

#### Scenario: DNS resolver 失败设置 errno
- **WHEN** DNS resolution fails because of invalid input, no answer, timeout, malformed response, capacity exhaustion, or UDP socket failure
- **THEN** the libc function MUST set userland `errno` to the documented positive error value and return the documented failure sentinel
- **AND** it MUST not leave partially successful output marked as valid

#### Scenario: DNS 超时使用 ETIMEDOUT
- **WHEN** DNS resolution fails because no matching response arrives within the bounded timeout
- **THEN** the libc function MUST set userland `errno` to `ETIMEDOUT`
- **AND** the positive `ETIMEDOUT` value MUST match the kernel single-source errno definition
