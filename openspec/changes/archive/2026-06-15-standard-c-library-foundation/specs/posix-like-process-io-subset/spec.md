## ADDED Requirements

### Requirement: Stage 40 libc 作为有界 POSIX-like 子集消费层

BigOS SHALL treat the Stage 40 C library foundation as a userland consumption layer over the existing bounded POSIX-like process and I/O subset. libc wrapper、headers、stdio/error reporting、file/process helpers 和 packaged tools MAY make the subset easier to consume from static C programs, but they MUST NOT expand the compatibility claim to complete POSIX process semantics, complete POSIX libc, complete shell behavior, sessions, terminal process groups, job control, termios, dynamic linking, shared libraries, async I/O, SMP, broad file-backed `mmap`, or persistent full writable filesystem semantics.

#### Scenario: libc wrapper 不扩大内核语义

- **WHEN** Stage 40 libc 为已有 process、fd、pipe、signal、time、identity、cwd 或 filesystem syscall 提供更清晰 wrapper
- **THEN** wrapper MUST preserve the documented bounded kernel/user behavior and errno translation
- **AND** wrapper MUST NOT imply unsupported POSIX semantics beyond the existing bounded subset

#### Scenario: packaged tools 使用有界子集

- **WHEN** packaged user programs or shell utilities are updated to consume Stage 40 libc helpers
- **THEN** their behavior MUST remain within the documented bounded process/I/O and filesystem subset
- **AND** tool output, diagnostics, or docs MUST NOT present the result as broad POSIX utility compatibility

#### Scenario: DIR star wrapper 不扩大目录语义

- **WHEN** Stage 40 libc introduces a `DIR*`-style directory enumeration wrapper over the existing bounded directory capability
- **THEN** the wrapper MUST preserve the documented bounded filesystem and errno behavior
- **AND** it MUST NOT imply complete POSIX directory traversal, complete `struct dirent`, ordering, snapshots, symlink traversal, mount namespace behavior, or directory fd semantics

### Requirement: Stage 40 兼容性文档保持负面边界

BigOS documentation, OpenSpec artifacts, validation notes, and user-facing descriptions SHALL keep explicit negative boundaries when describing Stage 40 userland compatibility. They MUST state that dynamic linking、shared libraries、dynamic loader、complete POSIX libc、complete POSIX shell、job control、terminal process groups、sessions、full terminal control、broad file-backed `mmap` and async I/O remain out of scope until separate later changes intentionally add them.

#### Scenario: 文档包含非目标

- **WHEN** Stage 40 文档或 validation notes 描述 libc、shell 工具或 POSIX-like wrapper 行为
- **THEN** 它们 MUST 同时标明这些行为属于 bounded subset
- **AND** MUST list or reference the relevant unsupported broad POSIX/runtime features as non-goals

#### Scenario: 验证不重新解释 unsupported behavior

- **WHEN** validation exercises a behavior outside the documented Stage 40 libc and process/I/O subset
- **THEN** unsupported, missing, or deterministic failure behavior MUST NOT be counted as POSIX compatibility success
- **AND** validation records MUST distinguish supported subset behavior from intentionally unsupported broad POSIX behavior
