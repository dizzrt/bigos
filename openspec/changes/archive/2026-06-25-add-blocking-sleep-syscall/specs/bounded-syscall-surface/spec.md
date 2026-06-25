## ADDED Requirements

### Requirement: 有界阻塞式 sleep syscall surface
BigOS SHALL extend the bounded syscall surface with an append-only blocking sleep operation that accepts a millisecond duration and returns success only after the current user process has slept until the coarse monotonic tick deadline expires. The operation MUST preserve the existing `int 0x80` ABI, deterministic negative errno convention, syscall no-EOI rule, and current syscall numbering for all previously defined syscalls.

#### Scenario: sleep syscall 追加到现有 surface
- **WHEN** the blocking sleep syscall is introduced
- **THEN** BigOS MUST add the syscall through an append-only number such as `SYS_SLEEP_MS`
- **AND** existing syscall numbers, register argument order, and return-value conventions MUST remain stable

#### Scenario: syscall 参数使用毫秒单位
- **WHEN** a user process calls the blocking sleep syscall with a supported millisecond duration in `rdi`
- **THEN** BigOS MUST interpret the value as milliseconds rather than seconds, ticks, nanoseconds, or a user pointer
- **AND** the syscall MUST return 0 on normal timeout completion

#### Scenario: syscall 错误不泄漏 scheduler 私有值
- **WHEN** the scheduler sleep primitive reports timeout completion, forbidden blocking, or another internal wait result
- **THEN** the syscall layer MUST translate the result into the documented user-visible success or negative errno value
- **AND** it MUST NOT expose scheduler-private constants such as `WAIT_TIMEOUT` or `WAIT_BLOCK_FORBIDDEN` directly to user mode
