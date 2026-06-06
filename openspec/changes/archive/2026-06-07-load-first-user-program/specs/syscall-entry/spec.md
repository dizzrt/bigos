## MODIFIED Requirements

### Requirement: 系统调用入口机制

BigOS SHALL provide a controlled software-triggered kernel entry path so kernel or user code can enter syscall handling without relying on external IRQs or CPU exceptions. BigOS SHALL continue to reuse the kernel-owned static IDT and existing `InterruptFrame` dispatch framework, using `int 0x80` as the syscall entry. When user mode is introduced, the syscall vector gate SHALL be allowed to accept CPL3 callers while preserving exception/IRQ/syscall dispatch separation.

#### Scenario: syscall vector 被 dispatch 识别并路由

- **WHEN** kernel or user code executes the syscall instruction sequence entering the agreed syscall vector (`VECTOR_SYSCALL = 0x80`)
- **THEN** interrupt dispatch MUST identify that vector as a syscall rather than a CPU exception or external IRQ
- **AND** dispatch MUST route control to syscall handling and return through the existing architecture return path when the syscall is expected to return

#### Scenario: syscall 路径不发送外部 IRQ EOI

- **WHEN** syscall vector 被处理
- **THEN** 该路径 MUST NOT 发送 i8259 EOI（syscall 不是外部 IRQ）
- **AND** CPU 异常、外部 IRQ 与 syscall 三类入口的 EOI 语义 MUST 保持分离不变

#### Scenario: 保持既有中断契约并显式开放 syscall gate

- **WHEN** ring3 syscall support is introduced for the first user program
- **THEN** kernel-owned static IDT, `InterruptFrame` field layout, dispatch ABI, and existing CPU exception versus external IRQ paths MUST remain unchanged
- **AND** only the syscall vector gate MAY be configured to allow CPL3 software entry; unrelated exception and IRQ gates MUST NOT be relaxed for user software entry
- **AND** required user segment and TSS/kernel-stack state MAY be introduced only as part of the controlled ring3 runtime path

### Requirement: 本阶段不进入用户态

BigOS SHALL narrow the earlier syscall-entry phase restriction: standalone syscall-entry bring-up and ring0 smoke MAY remain kernel-only, but a later first-user-program runtime path MAY enter ring3, switch to a user address space, load a user program, and invoke the syscall entry from user mode under explicit process-runtime requirements.

#### Scenario: syscall 自测仍可从内核态触发

- **WHEN** syscall entry is tested without enabling the first user program runtime
- **THEN** the existing ring0/kernel self-test path MAY continue to trigger syscall from kernel mode
- **AND** that self-test MUST NOT by itself require loading a user ELF, switching to ring3, or switching CR3

#### Scenario: 首个用户程序可从 ring3 触发 syscall

- **WHEN** the first user program runtime is enabled and has entered CPL3
- **THEN** BigOS MAY allow the user program to trigger `VECTOR_SYSCALL = 0x80` from ring3
- **AND** the syscall gate, kernel stack return mechanism, and dispatcher MUST be initialized before the user program executes that instruction
- **AND** syscall handling MUST return to user mode only for syscalls whose semantics permit returning

#### Scenario: #PF 内核诊断语义保留

- **WHEN** syscall-entry requirements are extended for user mode
- **THEN** kernel-mode `#PF` behavior MUST remain diagnostic-only
- **AND** user-mode fault handling MUST be specified by the user process capability and MUST NOT silently recover kernel faults

### Requirement: 系统调用入口的验证可复现

BigOS SHALL use source-level checks and default-off emulator smoke to validate syscall entry wiring, ABI register conventions, dispatch routing, unknown-number error returns, and user-mode syscall entry when the first user program runtime is enabled.

#### Scenario: 源码级检查覆盖入口与 ABI 不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover: syscall vector is recognized in dispatch and does not send EOI, number/argument/return-value register conventions, known number routing, unknown number deterministic error return, diagnostic or user-visible syscall marker/return behavior
- **AND** source-level checks MUST confirm only the syscall vector is relaxed for CPL3 software entry when user mode support is enabled

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` / cross-toolchain build, relevant `uv run pytest` source checks, and `openspec validate load-first-user-program --strict`
- **AND** if Bochs runtime smoke cannot observe syscall or user markers due to emulator, ROM, serial oracle, image lock, or interaction limits, validation MUST record the missing dependency and remaining bootability risk

## ADDED Requirements

### Requirement: 用户态 syscall 参数安全边界

BigOS SHALL validate or safely copy user-provided syscall pointers and bounded lengths before kernel code reads user memory on behalf of a ring3 caller.

#### Scenario: 用户 buffer 在读取前被检查

- **WHEN** a ring3 caller passes a pointer and length to a syscall such as write
- **THEN** BigOS MUST verify that the requested range is in user address space and mapped with user-accessible permissions, or use an equivalent safe copy helper that detects invalid access
- **AND** BigOS MUST bound the maximum copied or printed length for early smoke syscalls

#### Scenario: 非法用户参数返回错误或终止进程

- **WHEN** a ring3 caller passes an unmapped pointer, kernel-space pointer, non-user mapping, or overlong length
- **THEN** BigOS MUST return a deterministic negative error or terminate the current user process
- **AND** BigOS MUST NOT read arbitrary kernel memory or treat the syscall as successful

### Requirement: 用户态 write 和 exit syscall

BigOS SHALL provide a minimal user-visible syscall pair sufficient for the first user program to prove a user-to-kernel write path and a controlled process exit path.

#### Scenario: write syscall 输出用户程序 marker

- **WHEN** the first user program invokes the write syscall with a valid bounded user buffer
- **THEN** the syscall MUST emit the requested bounded content or a deterministic `BIGOS_USER_` marker through the existing console/serial diagnostic path
- **AND** the syscall MUST return a deterministic byte count or success code through the syscall return register

#### Scenario: exit syscall 不返回到已终止用户流

- **WHEN** the first user program invokes the exit syscall
- **THEN** BigOS MUST mark the current user process terminated and record the requested exit code
- **AND** BigOS MUST NOT return to the terminated user instruction stream
