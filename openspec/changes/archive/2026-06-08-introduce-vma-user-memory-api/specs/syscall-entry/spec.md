## ADDED Requirements

### Requirement: 用户内存 syscall ABI

BigOS SHALL extend the `int 0x80` syscall dispatch table with minimal user memory operations needed for VMA-backed userland bring-up. The extension MUST preserve the existing syscall vector, register ABI, exception/IRQ/syscall dispatch separation, and no-EOI syscall rule.

#### Scenario: brk syscall 调整 heap

- **WHEN** a ring3 caller invokes the `brk` syscall with a requested user break address
- **THEN** syscall handling MUST route the request to the current process heap VMA policy and return the committed break or deterministic negative error through the syscall return register
- **AND** it MUST NOT change the existing `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention

#### Scenario: anonymous mapping syscall 创建受限映射

- **WHEN** a ring3 caller invokes the supported anonymous mapping syscall or equivalent user-memory API entry
- **THEN** syscall handling MUST validate length, alignment, flags, permissions, VMA collisions, and allocation failure before returning a user address
- **AND** unsupported file-backed, shared, W+X, kernel-space, or overlarge requests MUST fail deterministically

### Requirement: syscall user-buffer validation uses VMA

BigOS SHALL validate user pointers passed to syscalls through VMA-backed range checks plus page-table accessibility or equivalent safe-copy checks before reading from or writing to user memory.

#### Scenario: read-only syscall buffer

- **WHEN** a syscall such as `write` or `open` receives a user source pointer and length
- **THEN** syscall handling MUST confirm the complete range is covered by readable user VMAs and accessible user mappings before copying bytes into kernel-owned memory
- **AND** invalid ranges MUST return a deterministic negative error or terminate the process through the documented user fault path

#### Scenario: writable syscall buffer

- **WHEN** a syscall such as `read` receives a user destination pointer and length
- **THEN** syscall handling MUST confirm the complete range is covered by writable user VMAs and accessible user mappings before copying bytes to user memory
- **AND** it MUST NOT write to kernel addresses, read-only VMAs, executable-only VMAs, unmapped pages, or overflowed ranges

### Requirement: 用户内存 syscall 上下文安全

BigOS SHALL ensure `brk`, anonymous mapping, and VMA-backed user-copy operations run only in contexts where allocation, blocking rules, and process lookup are valid.

#### Scenario: ordinary user syscall may allocate

- **WHEN** a current user process invokes `brk` or anonymous mapping from ordinary syscall context and blocking/allocation are allowed
- **THEN** syscall handling MAY allocate VMA metadata, user pages, and dynamic page-table pages according to the user-memory API contract

#### Scenario: nonblocking context rejects user memory syscall

- **WHEN** user memory syscall logic is reached from IRQ context, a preemption-disabled scheduler critical section, no-current-process context, or another context that must not allocate or block
- **THEN** BigOS MUST reject the operation or enter a documented diagnostic path
- **AND** it MUST NOT allocate VMA metadata, perform blocking filesystem work, or publish partial user mappings from that context
