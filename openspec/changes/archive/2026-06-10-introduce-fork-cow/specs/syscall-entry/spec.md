## ADDED Requirements

### Requirement: SYS_FORK syscall ABI

BigOS SHALL extend the `int 0x80` syscall dispatch table with a `SYS_FORK` operation that duplicates the current user process. `SYS_FORK` MUST return the new child PID to the parent and `0` to the child through the syscall return register, and MUST return a deterministic negative error (negated `bigos` errno) to the parent on failure. Adding `SYS_FORK` MUST preserve the existing syscall vector, the `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention, exception/IRQ/syscall dispatch separation, and the no-EOI syscall handling rule.

#### Scenario: fork syscall 被 dispatch 路由

- **WHEN** a ring3 caller issues `int 0x80` with the `SYS_FORK` number in `rax`
- **THEN** the dispatcher MUST route to the fork implementation and write the child PID (parent) or `0` (child) back into the return register
- **AND** the dispatcher MUST NOT send an i8259 EOI for the syscall and MUST NOT relax any exception or external IRQ gate

#### Scenario: fork 失败返回负 errno

- **WHEN** the fork implementation cannot create a child (allocation failure or process soft-limit reached)
- **THEN** the dispatcher MUST return a deterministic negative `bigos` errno (such as `-ENOMEM` or `-EAGAIN`) to the parent through the return register
- **AND** the unknown-number, register-convention, and dispatch-separation guarantees of the existing ABI MUST remain unchanged
