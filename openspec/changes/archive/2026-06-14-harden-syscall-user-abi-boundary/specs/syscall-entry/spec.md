## ADDED Requirements

### Requirement: syscall ABI 单一来源
BigOS SHALL maintain a single documented source for the current `int 0x80` syscall ABI, including syscall numbers, argument register order, return register, negative errno convention, and the mapping between user-visible wrapper arguments and kernel dispatcher inputs. Kernel dispatcher code, userland wrapper code, and bilingual documentation MUST stay consistent with that source.

#### Scenario: syscall number 和寄存器约定一致
- **WHEN** a syscall number or wrapper is added, removed, or renamed
- **THEN** the kernel dispatcher and userland wrapper MUST consume the same ABI number and argument ordering
- **AND** the documented register convention MUST remain `rax` for number/return and `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` for arguments unless a later explicit ABI-breaking change replaces it

#### Scenario: ABI 文档和源码不漂移
- **WHEN** syscall ABI facts are reviewed across source and documentation
- **THEN** docs in both English and Simplified Chinese MUST describe the same syscall vector, register order, return convention, and negative errno behavior
- **AND** any source-level assertion or consistency check MUST agree with those documented facts

### Requirement: syscall ABI 整理保持现有入口语义
BigOS SHALL preserve the current syscall entry behavior while hardening the kernel/user ABI boundary. The cleanup MUST NOT change the syscall vector, relax non-syscall IDT gates, send i8259 EOI for syscall handling, or introduce a second syscall instruction path.

#### Scenario: syscall vector 保持稳定
- **WHEN** the syscall/user ABI boundary is refactored
- **THEN** userland syscalls MUST continue to enter through the existing software syscall vector
- **AND** exception, external IRQ, and syscall dispatch separation MUST remain unchanged

#### Scenario: 不引入新的 syscall backend
- **WHEN** the boundary cleanup is implemented
- **THEN** BigOS MUST NOT require `syscall/sysret`, UEFI, a second ISA backend, SMP, or a new runtime backend for the default build
- **AND** existing x86_64 Legacy BIOS validation paths MUST remain the runnable baseline

### Requirement: syscall 错误返回边界
BigOS SHALL keep kernel syscall failures represented as deterministic negative errno values at the kernel/user ABI boundary, while userland libc wrappers translate those values into positive `errno` plus the documented failure sentinel.

#### Scenario: 内核返回负 errno
- **WHEN** syscall dispatch rejects an unknown number, invalid user pointer, bad fd, unsupported flag, or other bounded error
- **THEN** the kernel syscall layer MUST return a deterministic negative errno through the syscall return register
- **AND** it MUST NOT expose uninitialized values, panic for ordinary user errors, or rely on userland to interpret kernel-private state

#### Scenario: 用户 wrapper 执行 errno 翻译
- **WHEN** a userland syscall wrapper receives a negative errno value from the kernel
- **THEN** the wrapper MUST set user-visible `errno` to the corresponding positive value
- **AND** it MUST return the documented failure sentinel for that wrapper

### Requirement: syscall wrapper 寄存器 contract 静态检查
BigOS SHALL provide a source-level static contract check for userland raw syscall primitives and wrappers. The check MUST verify that the wrapper source follows the documented register ABI and clobber contract: syscall number and return value in `rax`, arguments in `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`, and explicit clobbers for `rcx`, `r11`, and `memory` where inline assembly is used.

#### Scenario: 静态检查发现寄存器约定漂移
- **WHEN** a userland syscall primitive changes its inline assembly constraints, register-bound variables, or clobber list
- **THEN** the source-level contract check MUST fail if the primitive no longer binds the fourth argument to `r10`, the fifth to `r8`, the sixth to `r9`, or the return value to `rax`
- **AND** the check MUST fail if required `rcx`, `r11`, or `memory` clobbers are removed without a separate ABI-changing specification

#### Scenario: 静态检查补充但不替代运行时验证
- **WHEN** syscall wrapper source-level checks pass
- **THEN** implementation validation MUST still run the narrowest available build check
- **AND** runtime syscall or userland smoke SHOULD still be run when wrapper behavior or syscall dispatch behavior changes and the emulator environment supports it
