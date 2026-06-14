## ADDED Requirements

### Requirement: Shell 可用性硬化纳入有界进程与 I/O 子集

BigOS SHALL include hardened interactive shell command composition in the bounded POSIX-like process and I/O subset. This subset MUST cover shell-visible path handling, deterministic error reporting, bounded exit-status propagation, fd inheritance, fd duplication, redirection isolation, single-pipe composition, and packaged user-program consumption. This subset MUST NOT claim support for job control, background jobs, sessions, terminal process groups, termios, complete POSIX shell language, complete POSIX process semantics, async I/O, SMP, dynamic linking, or broad file-backed `mmap`.

#### Scenario: 文档描述 shell hardening 为有界子集

- **WHEN** BigOS documentation, OpenSpec artifacts, validation notes, or roadmap follow-up describe hardened shell behavior
- **THEN** they MUST describe it as bounded shell usability within the current process/I/O subset
- **AND** they MUST NOT imply complete POSIX shell, terminal, process-group, session, permission, or job-control support

#### Scenario: 简单程序可依赖组合边界

- **WHEN** a simple statically linked user program is launched by shell through supported PATH, cwd-relative path, redirection, or single-pipe composition
- **THEN** it MUST be able to rely on documented fd inheritance, stdin/stdout/stderr mapping, errno-based failures, and wait/exit observation
- **AND** it MUST NOT require hosted runtime, dynamic loader, complete POSIX libc, process groups, sessions, terminal control, or async I/O

### Requirement: Shell 组合失败不破坏父进程 fd 与 wait/reap 语义

BigOS SHALL preserve parent shell fd state and process lifecycle safety across supported shell command composition. Failed redirection, failed pipe setup, failed fork, failed exec, and child nonzero exit MUST be observable without corrupting the parent shell's standard descriptors, leaking published file descriptors, leaving unreaped eligible children, or requiring unsafe teardown from IRQ, exception-only, scheduler-critical, or preemption-disabled contexts.

#### Scenario: 失败 setup 保留父 shell 标准 fd

- **WHEN** shell cannot complete redirection or pipe setup before launching a command
- **THEN** the parent shell MUST retain usable stdin, stdout, and stderr mappings for subsequent commands
- **AND** unpublished intermediate fd objects MUST be closed or made unreachable according to existing fd lifecycle rules

#### Scenario: 子进程完成后可回收

- **WHEN** shell launches one or two child processes for a supported external command or single-pipe command
- **THEN** shell MUST wait for eligible children and observe bounded completion status
- **AND** process table slots and fd references MUST remain reusable after completion or failure

#### Scenario: 不可阻塞上下文不执行 shell I/O setup

- **WHEN** shell-related fd/VFS, pipe, dup, wait, or exec work is required
- **THEN** it MUST run through normal user process syscall context where blocking and allocation are allowed
- **AND** it MUST NOT require IRQ-context or scheduler-critical path file I/O setup
