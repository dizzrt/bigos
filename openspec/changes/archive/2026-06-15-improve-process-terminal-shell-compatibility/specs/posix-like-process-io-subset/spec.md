## ADDED Requirements

### Requirement: Stage 42 组合兼容性边界
BigOS SHALL include bounded process, terminal, and shell composition behavior in its POSIX-like process and I/O subset. The subset SHALL cover wait/waitpid status observation, signal-terminated child reporting, default terminal line-control input, shell error recovery, pipe/redirection fd isolation, and simple static user-program composition while continuing to reject complete POSIX process, terminal, shell, libc, SMP, async I/O, and broad mmap claims.

#### Scenario: 文档描述有界组合能力
- **WHEN** BigOS documentation, specs, help text, or validation notes describe Stage 42 process/terminal/shell compatibility
- **THEN** they MUST describe it as a bounded BigOS POSIX-like subset
- **AND** they MUST NOT imply complete POSIX shell, sessions, terminal process groups, job control, termios, dynamic linking, complete POSIX libc, SMP, async I/O, persistent full writable filesystem, or broad file-backed `mmap`

#### Scenario: 简单程序依赖组合子集
- **WHEN** a simple statically linked user program uses only documented wait, signal status, fd, pipe, redirection, cwd/path, and terminal stdin/stdout behavior
- **THEN** it MUST be able to rely on deterministic kernel/libc return values, errno translation, and shell-observable output within the bounded subset
- **AND** it MUST NOT require hosted OS services, shared libraries, threads, terminal process groups, or complete POSIX utilities

### Requirement: Stage 42 行为验证覆盖组合路径
BigOS SHALL provide layered validation for the Stage 42 process, terminal, and shell composition subset. Validation MUST separate build/static checks, runtime checks that passed, checks skipped due to missing environment, historical diagnostics, current-change diagnostics, and remaining risk.

#### Scenario: runtime validation observes process and shell status
- **WHEN** runtime validation runs in a configured emulator environment with the required cross-toolchain and disk image
- **THEN** it MUST observe at least one shell-launched child normal exit, one deterministic command failure, and one wait-status-driven shell recovery path
- **AND** the result MUST be decidable from stdout/stderr, exit status, serial/log output, or another deterministic low-level signal

#### Scenario: runtime validation observes fd composition
- **WHEN** shell or user-program validation exercises supported pipe or redirection behavior
- **THEN** it MUST observe data reaching the intended endpoint and parent shell standard descriptors remaining usable afterward
- **AND** failures MUST leave an observable deterministic error rather than hanging the shell

#### Scenario: unavailable validation is recorded
- **WHEN** xmake, `x86_64-elf-*`, QEMU, Bochs, ROM/display dependencies, disk image generation, console input, or timeout oracles are unavailable
- **THEN** the corresponding validation MUST be skipped only with an explicit record of the missing condition
- **AND** the record MUST include substitute checks that were run and the remaining process/terminal/shell compatibility risk
