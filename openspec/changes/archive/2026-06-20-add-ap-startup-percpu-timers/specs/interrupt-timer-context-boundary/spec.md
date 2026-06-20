## MODIFIED Requirements

### Requirement: 边界清理保持单核 backend 范围

BigOS SHALL keep interrupt/timer/context boundary hardening compatible with the current x86_64 Legacy BIOS runnable backend while allowing the AP startup/per-CPU timer baseline to introduce bounded LAPIC/IOAPIC activation and to migrate the scheduler timer interrupt to the APIC-backed timer path. The boundary MUST NOT claim runtime parity for new backends or complete APIC-backed external interrupt delivery for all devices unless a later change explicitly expands that scope.

#### Scenario: 默认 backend 仍为 x86_64

- **WHEN** implementation, documentation, or validation describes the result of this change
- **THEN** it MUST state or preserve that the current runnable backend remains x86_64 Legacy BIOS/MBR/exFAT
- **AND** it MUST NOT require UEFI, non-x86 architecture support, HPET, new storage/device backends, or broad backend parity to build or validate the default path

#### Scenario: APIC/per-CPU timer 范围受控

- **WHEN** the interrupt/timer/context boundary is used for AP startup or per-CPU local timer activation
- **THEN** BigOS MAY initialize LAPIC, IOAPIC boundary state, timer routing, PIT-referenced calibration, and local APIC timer state needed for that activation
- **AND** it MUST NOT describe this as complete external IRQ migration for every device, full SMP scheduling, IPI-backed TLB shootdown, or APIC-backed default delivery for every interrupt source

#### Scenario: Future backend work remains separate

- **WHEN** the boundary exposes an interface that could be reused by a future backend
- **THEN** the interface MUST describe the current core semantic need rather than promising complete multi-architecture HAL behavior
- **AND** documentation MUST keep future backend parity as a non-goal for this change

### Requirement: 边界验证匹配触及范围

BigOS SHALL validate interrupt/timer/context boundary work with checks matched to the touched layer and record unavailable low-level validation explicitly. Documentation-only work can use OpenSpec/status checks, runtime boundary work requires a narrow build, and AP startup/per-CPU timer work requires bounded multi-core emulator validation when supported by the local environment.

#### Scenario: Spec or documentation only boundary work

- **WHEN** a change only updates OpenSpec artifacts, roadmap-level planning, or architecture documentation without changing runtime control flow
- **THEN** OpenSpec status or validation checks and targeted consistency searches MUST be sufficient validation
- **AND** runtime emulator smoke MAY be skipped with the documentation-only scope recorded

#### Scenario: Runtime boundary work

- **WHEN** a change modifies C++ headers, C++ sources, assembly stubs, PIT/PIC/APIC drivers, IRQ dispatch, timer hooks, scheduler preemption, AP startup, or context switch code
- **THEN** validation MUST include the narrowest useful cross-toolchain build or explicitly record missing `x86_64-elf-gcc`/xmake dependencies
- **AND** automated smoke validation SHOULD use QEMU headless first when the local environment supports it
- **AND** Bochs MAY be used for early manual testing or high-risk hardware-behavior cross-validation, and unavailable Bochs validation MUST be recorded as residual manual/cross-validation risk rather than a default automation blocker

#### Scenario: AP startup/per-CPU timer validation is staged

- **WHEN** a change enables AP startup, LAPIC timer, or per-CPU timer behavior
- **THEN** validation MUST include a bounded multi-core emulator smoke when local QEMU or Bochs APIC configuration is available
- **AND** the validation MUST observe AP online acknowledgement, PIT-referenced timer calibration or its deterministic failure path, and CPU-local timer progress without claiming complete cross-CPU scheduling or complete APIC external IRQ migration for all devices

#### Scenario: EOI ordering validation is staged

- **WHEN** EOI ordering is reviewed for timer preemption, LAPIC timer, APIC interrupt delivery, or IRQ-return scheduling work
- **THEN** targeted consistency search and source-level review notes MUST confirm whether the interrupt source is owned by legacy i8259 EOI, LAPIC EOI, or no external EOI such as syscall dispatch
- **AND** a dedicated static-check script is required only when runtime control-flow changes expose an EOI-ordering blind spot that review/search cannot cover repeatably
