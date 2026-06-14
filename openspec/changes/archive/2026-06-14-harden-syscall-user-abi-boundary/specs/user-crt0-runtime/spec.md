## ADDED Requirements

### Requirement: crt0 入口只依赖稳定用户入口契约
BigOS userland crt0 SHALL depend only on the documented user ELF entry contract, initial user stack layout, C calling convention needed to call `main`, and the stable syscall ABI needed to terminate the process. crt0 MUST NOT depend on kernel-private process structs, VMA metadata, interrupt frame layout, scheduler internals, or undocumented ring3 entry implementation details.

#### Scenario: crt0 读取文档化初始栈
- **WHEN** a statically linked user program starts at `_start`
- **THEN** crt0 MUST read `argc`, NULL-terminated `argv`, and `envp` according to the documented initial user stack contract
- **AND** it MUST NOT rely on hidden kernel stack contents or backend-private handoff state

#### Scenario: crt0 退出经稳定 syscall ABI
- **WHEN** `main` returns from a user program
- **THEN** crt0 MUST terminate the process through the documented exit syscall wrapper or direct stable syscall ABI
- **AND** it MUST NOT return to an undefined caller or use a kernel-private termination path

### Requirement: crt0 边界整理保持 freestanding runtime
BigOS userland crt0 SHALL remain freestanding and static-link friendly while syscall/user ABI boundaries are hardened. The cleanup MUST NOT introduce hosted runtime initialization, dynamic linking, C++ global constructors, thread runtime, or host libc dependencies.

#### Scenario: crt0 构建保持 freestanding
- **WHEN** user programs are linked with crt0 during the default build
- **THEN** crt0 MUST remain compatible with `-nostdlib -static` user program linking
- **AND** it MUST NOT require host libc, a dynamic loader, shared libraries, exceptions, RTTI, or thread runtime support

#### Scenario: crt0 不扩大用户态 ABI
- **WHEN** crt0 is updated for ABI boundary cleanup
- **THEN** it MUST preserve the existing entry, stack, and exit behavior for supported static C programs
- **AND** it MUST NOT add new user-visible ABI requirements unless corresponding specs and docs are updated
