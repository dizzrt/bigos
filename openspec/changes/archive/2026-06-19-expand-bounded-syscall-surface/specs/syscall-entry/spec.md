## ADDED Requirements

### Requirement: bounded syscall surface ABI extension

BigOS SHALL extend the existing `int 0x80` syscall table only by appending bounded syscall numbers for wait variants, fd control, file/path primitives, and process information primitives. The extension MUST preserve the existing syscall vector, register argument mapping, negative errno return convention, and behavior of already-published syscall numbers.

#### Scenario: 追加 syscall 编号不重排既有 ABI

- **WHEN** the bounded syscall surface adds a new kernel entry
- **THEN** BigOS MUST assign it without changing the numeric values, argument registers, or return convention of existing syscall numbers
- **AND** unknown or unsupported syscall numbers MUST continue to return deterministic `ENOSYS`-style failure

#### Scenario: 用户指针 copy 有界

- **WHEN** a new syscall accepts a user path, metadata buffer, status pointer, fd-control argument, or other user memory pointer
- **THEN** BigOS MUST validate and copy the user memory through the existing VMA-backed user-buffer boundary before dereferencing it in kernel code
- **AND** invalid, unmapped, overlong, or non-NUL-terminated user data MUST fail deterministically without publishing partial state

### Requirement: syscall blocking-context boundary for expanded surface

BigOS SHALL execute expanded syscall operations that may allocate, wait, access fd tables, resolve paths, or perform synchronous storage I/O only from ordinary user process syscall context where blocking is allowed. These operations MUST NOT run their full logic from IRQ, exception-only diagnostic paths, preemption-disabled scheduler critical sections, or other non-blockable contexts.

#### Scenario: 普通 syscall 上下文执行扩展操作

- **WHEN** a user process invokes an expanded wait, fd-control, file/path, or process-information syscall from ordinary syscall context
- **THEN** BigOS MAY access process state, fd state, VFS state, and bounded user memory according to each syscall contract
- **AND** the dispatcher MUST preserve the existing `int 0x80` entry and return path

#### Scenario: 不可阻塞上下文拒绝扩展操作

- **WHEN** expanded syscall logic would be invoked from IRQ, preemption-disabled, scheduler-critical, or otherwise non-blockable context
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT perform synchronous storage I/O, unbounded allocation, blocking wait, or user-pointer copy from that context
