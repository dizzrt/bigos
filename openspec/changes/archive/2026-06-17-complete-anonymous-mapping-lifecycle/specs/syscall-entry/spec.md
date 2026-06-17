## ADDED Requirements

### Requirement: 匿名映射生命周期 syscall ABI

BigOS SHALL extend the existing `int 0x80` syscall dispatch table with bounded user memory operations for anonymous unmap and protection change. The extension MUST preserve the existing syscall vector, register ABI, exception/IRQ/syscall dispatch separation, no-EOI syscall rule, and deterministic negative-error convention.

#### Scenario: unmap syscall 路由到当前进程 VMA 生命周期

- **WHEN** a ring3 caller invokes the bounded anonymous unmap syscall with a user address and length
- **THEN** syscall handling MUST validate the page-aligned range, current process state, user low-half bounds, overflow, and context safety before routing to the current process VMA unmap operation
- **AND** the syscall return register MUST contain success or a deterministic negative error without changing the existing `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention

#### Scenario: protection syscall 路由到当前进程 VMA 权限策略

- **WHEN** a ring3 caller invokes the bounded protection-change syscall with a user address, length, and permission flags
- **THEN** syscall handling MUST validate alignment, range, supported permissions, W+X rejection, current process state, and context safety before routing to the current process VMA protection-change operation
- **AND** the syscall return register MUST contain success or a deterministic negative error without relaxing exception or IRQ gates

#### Scenario: unsupported lifecycle syscall 请求确定性失败

- **WHEN** a lifecycle syscall receives unsupported flags, file-backed writable semantics, shared writable semantics, kernel-space addresses, an overflowing range, or an incompatible current context
- **THEN** BigOS MUST return a deterministic negative error or terminate the process through the documented user fault path if the syscall arguments themselves cannot be safely read
- **AND** it MUST NOT read arbitrary kernel memory, send i8259 EOI for syscall, or publish a partial VMA/page-table change as success

### Requirement: 用户态 wrapper 暴露有界生命周期语义

BigOS SHALL expose minimal freestanding userland wrappers for the bounded anonymous unmap and protection-change syscalls. The wrappers MUST document and preserve the bounded semantics: page-aligned ranges, anonymous/private focus, deterministic negative errors, no full POSIX compatibility claim, and no support for `MAP_FIXED`, shared writable mapping, or file-backed writable mapping.

#### Scenario: user wrapper 返回 syscall 结果

- **WHEN** a user program calls the anonymous unmap or protection-change wrapper with supported arguments
- **THEN** the wrapper MUST invoke the corresponding syscall through the existing user syscall ABI
- **AND** it MUST return the kernel result without requiring hosted libc, dynamic linking, environment variables, or OS services outside the BigOS freestanding userland

#### Scenario: wrapper 不声明完整 POSIX 语义

- **WHEN** user-facing headers or small programs reference the anonymous lifecycle wrappers
- **THEN** they MUST describe the bounded BigOS semantics rather than claiming complete POSIX `munmap` or `mprotect`
- **AND** unsupported flags and unaligned ranges MUST remain caller-visible failures rather than being silently normalized into successful operations
