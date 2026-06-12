## ADDED Requirements

### Requirement: 组合式进程和 fd 行为验证可由运行时结果判定

BigOS SHALL validate representative combined behavior for the bounded POSIX-like process and I/O subset through runtime-observable results. Validation MUST cover process lifecycle, exec/wait, fd inheritance, duplication, redirection, pipe behavior, user-visible errors, and shell command composition within the documented bounded subset.

#### Scenario: exec wait 和 fd 继承组合行为可观察

- **WHEN** process/I/O behavior validation launches a supported child program through the shell or an equivalent deterministic userland path
- **THEN** validation MUST observe the child program output, parent wait result, exit status, and inherited or redirected fd behavior
- **AND** the result MUST be decidable from runtime output, file contents, fd endpoint effects, serial/log output, or another deterministic low-level signal

#### Scenario: pipe 和 redirection 端点效果可验证

- **WHEN** process/I/O behavior validation exercises supported pipe or redirection behavior
- **THEN** validation MUST observe that data reaches the intended downstream command, file, or fd endpoint
- **AND** unrelated fd state MUST remain usable after the operation

#### Scenario: unsupported shell 或 I/O 形式可见失败

- **WHEN** validation exercises a command, redirection, pipe, or syntax form outside the bounded supported subset
- **THEN** BigOS MUST report failure or unsupported behavior through an observable error path
- **AND** validation MUST NOT reinterpret unsupported behavior as successful POSIX compatibility

### Requirement: 有界用户态兼容性验证不暗示完整 POSIX

BigOS SHALL present behavior-oriented userland compatibility validation as coverage for the documented bounded process and I/O subset only. Validation artifacts MUST distinguish supported behavior from explicitly unsupported POSIX features.

#### Scenario: supported subset 被标注为有界兼容

- **WHEN** validation notes, documentation, or OpenSpec artifacts describe shell, process/fd, pipe, redirection, or filesystem behavior
- **THEN** they MUST describe the behavior as a bounded BigOS-compatible subset
- **AND** they MUST NOT imply complete POSIX process semantics, full shell grammar, sessions, terminal process groups, job control, complete permissions, dynamic linking, or a complete POSIX libc

#### Scenario: 跨 backend 规划不改变当前行为边界

- **WHEN** behavior validation is used to protect later refactoring or backend work
- **THEN** the current runnable validation target MUST remain the x86_64 Legacy BIOS/MBR/exFAT path unless a separate backend change explicitly expands it
- **AND** validation records MUST call out any backend-specific assumptions that affect runtime-observable behavior
