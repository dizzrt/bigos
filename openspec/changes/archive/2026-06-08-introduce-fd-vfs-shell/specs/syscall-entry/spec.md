## ADDED Requirements

### Requirement: 用户态 open/read/close syscall
BigOS SHALL extend the existing `int 0x80` syscall dispatch table with user-visible `open`, `read`, and `close` operations backed by the process fd table and VFS shell. These syscalls MUST preserve the existing syscall vector, register ABI, exception/IRQ/syscall dispatch separation, and no-EOI syscall handling rule.

#### Scenario: open syscall 复制用户 path
- **WHEN** a ring3 caller invokes `open` with a user pointer to a bounded NUL-terminated path and read-only flags
- **THEN** syscall handling MUST validate and copy the path into kernel-owned memory before calling VFS
- **AND** the return register MUST contain a process-local fd on success or a deterministic negative error on failure

#### Scenario: read syscall 复制到用户 buffer
- **WHEN** a ring3 caller invokes `read(fd, buffer, length)` with a valid readable fd and valid user destination range
- **THEN** syscall handling MUST read through the fd table and VFS file object, copy at most the requested bytes into the user buffer, and return the byte count through the syscall return register

#### Scenario: close syscall 释放 fd
- **WHEN** a ring3 caller invokes `close(fd)` with a valid open descriptor
- **THEN** syscall handling MUST remove the descriptor from the current process fd table, drop the file reference, and return success deterministically

#### Scenario: fd syscall 错误不破坏现有 ABI
- **WHEN** `open`, `read`, or `close` receives an invalid user pointer, unsupported flags, overlong path, bad fd, non-readable file, or VFS/backend failure
- **THEN** syscall handling MUST return a deterministic negative error or terminate the current process through the documented user fault path
- **AND** it MUST NOT relax exception/IRQ gates, send i8259 EOI for syscall, or change the `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention

### Requirement: fd syscall 阻塞上下文安全
BigOS SHALL ensure fd-related syscalls run only when blocking, allocation, and synchronous filesystem reads are permitted. The syscall layer MUST guard against fd/VFS operations from nonblocking contexts and MUST keep IRQ and scheduler critical-section rules explicit.

#### Scenario: 普通用户 syscall 可进入 VFS
- **WHEN** a user process invokes `open` or `read` from normal syscall context and the current thread may block
- **THEN** syscall handling MAY enter VFS and the read-only exFAT backend under the fd/VFS blocking contract

#### Scenario: 非阻塞上下文拒绝 fd syscall
- **WHEN** fd syscall logic is reached from IRQ context, preemption-disabled scheduler critical section, or another context that must not block
- **THEN** BigOS MUST return a deterministic error or enter a documented diagnostic path
- **AND** it MUST NOT perform blocking ATA PIO reads, enqueue wait states, or allocate VFS/file/fd objects from that context
