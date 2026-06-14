## Purpose

Define the BigOS interrupt, timer, context-switch, and scheduler-facing architecture boundary for the current single-core x86_64 backend without claiming a complete HAL, SMP support, or new backend runtime parity.

## Requirements

### Requirement: 中断计时上下文边界所有权明确

BigOS SHALL define explicit ownership boundaries between architecture interrupt entry/exit, hardware timer/PIC drivers, timer tick policy, scheduler policy, and context-switch mechanics under the current single-core x86_64 backend.

#### Scenario: 评审中断到调度控制流

- **WHEN** 变更触及 exception/IRQ dispatch、PIT IRQ0、timer tick、scheduler preemption 或 context switch 路径
- **THEN** 评审记录必须能够区分 architecture-owned entry/exit、driver-owned hardware programming、timer-owned tick state、scheduler-owned policy 和 context-switch-owned register/stack state
- **AND** portable kernel policy MUST NOT require direct knowledge of x86_64 descriptor layout, raw ISR stack offsets, or PIC/PIT port programming details outside the documented boundary

#### Scenario: 保留低层 ABI 假设

- **WHEN** interrupt/timer/context boundary cleanup 完成
- **THEN** BigOS MUST preserve existing interrupt vectors, syscall vector behavior, i8259 EOI rules, `InterruptFrame` semantics, context-switch frame layout, boot/linker addresses, page-table layout, disk layout, and user-visible syscall ABI
- **AND** any required change to those assumptions MUST be split into a separate OpenSpec change

### Requirement: Architecture context header is narrow and semantic

BigOS SHALL introduce a small architecture-context boundary header for scheduler-facing and interrupt-return context semantics without exposing x86_64 private frame, descriptor, or hardware programming details as portable core contracts.

#### Scenario: Core consumes context semantics

- **WHEN** scheduler, timer preemption, or IRQ-return code needs to ask whether a context may be preempted or switched
- **THEN** it MUST consume the architecture-context boundary through semantic helpers or documented types
- **AND** it MUST NOT depend on raw x86_64 ISR stack offsets, descriptor fields, PIC/PIT port constants, or context-switch assembly offsets outside the architecture-owned implementation

#### Scenario: Header does not promise backend parity

- **WHEN** the architecture-context header is documented or reviewed
- **THEN** it MUST be described as the current x86_64 backend's narrow core-facing context boundary
- **AND** it MUST NOT claim complete HAL behavior, SMP support, UEFI runtime parity, non-x86 runtime parity, APIC/IOAPIC support, or HPET support

### Requirement: IRQ-return 调度边界可审计

BigOS SHALL make timer-driven IRQ-return scheduling interactions explicit and auditable without moving scheduler policy into architecture ISR assembly or hardware driver code.

#### Scenario: Timer IRQ records bounded scheduler intent

- **WHEN** PIT IRQ0 observes timer progress while scheduler preemption accounting is enabled
- **THEN** the timer path MAY notify scheduler-owned bounded state that a reschedule is desired
- **AND** the notification MUST NOT allocate memory, free memory, block, call delay loops, access filesystem services, depend on user-mode services, or send i8259 EOI directly

#### Scenario: Context switch occurs only at safe boundary

- **WHEN** a pending timer-driven reschedule reaches an IRQ-return or scheduler-owned safe boundary
- **THEN** BigOS MUST verify preemption is enabled, the interrupted context is eligible, the current thread remains switchable, and saved frame/context semantics remain valid before switching
- **AND** BigOS MUST defer the switch when the path is inside a documented non-preemptible region, fatal diagnostic path, exception recovery boundary, syscall-forbidden region, or scheduler critical section

### Requirement: 边界清理保持单核 backend 范围

BigOS SHALL keep interrupt/timer/context boundary hardening scoped to the current single-core x86_64 Legacy BIOS runnable backend and MUST NOT claim runtime parity for new backends.

#### Scenario: 默认 backend 仍为 x86_64

- **WHEN** implementation, documentation, or validation describes the result of this change
- **THEN** it MUST state or preserve that the current runnable backend remains x86_64 Legacy BIOS/MBR/exFAT
- **AND** it MUST NOT require UEFI, non-x86 architecture support, APIC/IOAPIC, HPET, SMP, or new storage/device backends to build or validate the default path

#### Scenario: Future backend work remains separate

- **WHEN** the boundary exposes an interface that could be reused by a future backend
- **THEN** the interface MUST describe the current core semantic need rather than promising complete multi-architecture HAL behavior
- **AND** documentation MUST keep future backend parity as a non-goal for this change

### Requirement: 边界验证匹配触及范围

BigOS SHALL validate interrupt/timer/context boundary work with checks matched to the touched layer and record unavailable low-level validation explicitly.

#### Scenario: Spec or documentation only boundary work

- **WHEN** a change only updates OpenSpec artifacts, roadmap-level planning, or architecture documentation without changing runtime control flow
- **THEN** OpenSpec status or validation checks and targeted consistency searches MUST be sufficient validation
- **AND** runtime emulator smoke MAY be skipped with the documentation-only scope recorded

#### Scenario: Runtime boundary work

- **WHEN** a change modifies C++ headers, C++ sources, assembly stubs, PIT/PIC drivers, IRQ dispatch, timer hooks, scheduler preemption, or context switch code
- **THEN** validation MUST include the narrowest useful cross-toolchain build or explicitly record missing `x86_64-elf-gcc`/xmake dependencies
- **AND** automated smoke validation SHOULD use QEMU headless first when the local environment supports it
- **AND** Bochs MAY be used for early manual testing or high-risk hardware-behavior cross-validation, and unavailable Bochs validation MUST be recorded as residual manual/cross-validation risk rather than a default automation blocker

#### Scenario: EOI ordering validation is staged

- **WHEN** EOI ordering is reviewed for timer preemption or IRQ-return scheduling work
- **THEN** targeted consistency search and source-level review notes MUST confirm that external IRQ dispatch owns the single i8259 EOI and timer/scheduler hooks do not send EOI directly
- **AND** a dedicated static-check script is required only when runtime control-flow changes expose an EOI-ordering blind spot that review/search cannot cover repeatably
