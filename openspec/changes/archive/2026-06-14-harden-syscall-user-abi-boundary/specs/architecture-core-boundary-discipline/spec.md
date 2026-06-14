## ADDED Requirements

### Requirement: syscall/user ABI 核心消费边界
BigOS core code SHALL consume syscall and user ABI mechanisms through stable semantic boundaries rather than scattering x86_64-private register, descriptor, interrupt-frame, or assembly-entry details across unrelated core subsystems. x86_64 implementation details MAY remain in the architecture backend and low-level entry code, but core-facing interfaces MUST describe the kernel concept being consumed.

#### Scenario: 核心层使用 syscall 语义接口
- **WHEN** `kernel/core` code routes, validates, or documents syscall behavior
- **THEN** the code MUST express syscall number, argument extraction, return value, errno, and user-copy semantics through stable ABI helpers or documented interfaces
- **AND** unrelated core subsystems MUST NOT open-code x86_64 interrupt-frame offsets or descriptor state

#### Scenario: 架构私有细节留在 backend
- **WHEN** syscall entry requires IDT gate setup, TSS/RSP0 state, ring transition mechanics, or assembly frame handling
- **THEN** those details MUST remain in architecture-specific implementation or explicitly named low-level entry code
- **AND** the core contract MUST remain the stable user/kernel syscall semantics rather than the raw x86_64 mechanism

### Requirement: ABI 边界整理不改变低层布局假设
BigOS architecture boundary cleanup for syscall/user ABI SHALL preserve existing boot, linker, interrupt vector, page-table, CR3 switching, user stack, and disk layout assumptions unless a separate change explicitly declares and validates such a behavior change.

#### Scenario: syscall/user ABI cleanup touches low-level paths
- **WHEN** implementation work touches syscall entry, user entry, exec stack setup, or user wrapper behavior
- **THEN** existing vector, register order, return convention, stack layout, CR3 switching semantics, and no-EOI syscall rule MUST remain unchanged
- **AND** any required behavior change to those assumptions MUST be split into a separate OpenSpec change

#### Scenario: 默认 backend 保持可运行
- **WHEN** ABI boundary cleanup is complete
- **THEN** the default runnable backend MUST remain the current x86_64 Legacy BIOS/MBR/exFAT path
- **AND** documentation MUST NOT claim runnable multi-architecture, UEFI, SMP, or runtime-backend parity support

### Requirement: ABI 边界验证匹配触及范围
BigOS SHALL validate syscall/user ABI boundary changes with checks appropriate to the touched layer. Specification-only or documentation-only work MUST at least pass OpenSpec status and targeted consistency searches; source changes to syscall entry, wrappers, crt0, user program build, or public headers MUST run the narrowest useful build or smoke that the local toolchain and emulator environment supports.

#### Scenario: 文档和规格整理
- **WHEN** a change only updates OpenSpec artifacts or documentation for syscall/user ABI boundaries
- **THEN** OpenSpec status checks and targeted consistency searches MUST be recorded
- **AND** QEMU or Bochs runtime smoke MAY be skipped with that scope explicitly stated

#### Scenario: 源码行为整理
- **WHEN** a change updates syscall dispatcher behavior, ABI headers, userland wrappers, crt0, user program build rules, or user-entry control flow
- **THEN** the implementation MUST run the narrowest available build check
- **AND** when the environment supports it, a matching syscall or userland headless QEMU smoke SHOULD be run and recorded
