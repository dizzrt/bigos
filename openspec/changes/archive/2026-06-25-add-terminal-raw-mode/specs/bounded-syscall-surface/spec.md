## ADDED Requirements

### Requirement: 有界默认终端模式 syscall

BigOS SHALL extend the bounded syscall surface with append-only default terminal mode operations that let simple static user programs query and set the single default terminal's canonical/raw input mode. The operations MUST preserve the existing `int 0x80` vector, register ABI, return convention, syscall no-EOI rule, and deterministic negative errno behavior.

#### Scenario: 查询 terminal mode syscall

- **WHEN** a user process invokes the supported terminal-mode query syscall with a valid user output buffer or equivalent register-return contract
- **THEN** BigOS MUST return the current default terminal mode deterministically
- **AND** the syscall MUST NOT mutate TTY input state, foreground process group state, fd state, or console render state

#### Scenario: 设置 terminal mode syscall

- **WHEN** a permitted foreground/session process invokes the supported terminal-mode set syscall with a valid canonical or raw mode request
- **THEN** BigOS MUST update the default terminal mode and return success
- **AND** subsequent default terminal reads MUST observe the requested mode

#### Scenario: 非法 mode 或非法用户缓冲失败

- **WHEN** a terminal-mode syscall receives an invalid user pointer, unsupported mode value, unknown flag, invalid structure size, or request from a disallowed process
- **THEN** BigOS MUST return deterministic negative errno or follow the documented user fault path
- **AND** it MUST NOT partially update terminal mode or corrupt TTY buffers

#### Scenario: syscall ABI 追加而不改号

- **WHEN** terminal-mode syscalls are added
- **THEN** BigOS MUST append new syscall numbers or otherwise extend only documented unused entries
- **AND** existing syscall numbers, register argument order, `VECTOR_SYSCALL = 0x80`, DPL/syscall gate behavior, and syscall EOI semantics MUST remain unchanged
