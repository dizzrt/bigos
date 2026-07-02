## ADDED Requirements

### Requirement: DNS 超时错误码常量
BigOS SHALL add `ETIMEDOUT` to the single kernel errno source `include/bigos/errno.h` and the user-space errno mirror in an append-only manner. The value MUST use the conventional POSIX/Linux x86_64 value 110, MUST remain freestanding-safe as a compile-time integer constant, and MUST follow the existing convention that kernel syscall paths return the negated errno value. Adding `ETIMEDOUT` MUST NOT change any existing errno value or existing syscall behavior.

#### Scenario: ETIMEDOUT 集中定义且数值稳定
- **WHEN** DNS resolver or future bounded timeout-facing paths need to report a timed-out operation through errno
- **THEN** `ETIMEDOUT` MUST be defined in `include/bigos/errno.h` with value 110
- **AND** no subsystem header or source file MUST introduce a second independent numeric definition for the same errno

#### Scenario: 用户态 errno mirror 保持一致
- **WHEN** userland libc exposes `ETIMEDOUT` through its errno header
- **THEN** the positive value MUST match the kernel single-source definition exactly
- **AND** existing source-contract checks for kernel/user errno equality MUST include or otherwise cover `ETIMEDOUT`

#### Scenario: 新增不改变既有错误码
- **WHEN** `ETIMEDOUT` is added for DNS timeout/no-response behavior
- **THEN** all existing errno constants MUST keep their prior numeric values and observable negative syscall return values
- **AND** the addition MUST NOT alter existing socket, fd, VFS, process, or signal error behavior
