## ADDED Requirements

### Requirement: Runtime smoke matrix covers scheduler semantics
BigOS SHALL extend the runtime smoke validation matrix with narrow scheduler semantics cases that validate timer-driven preemption, preemption-disable behavior, and preservation of existing cooperative scheduler behavior.

#### Scenario: Matrix lists scheduler semantics cases
- **WHEN** a developer inspects the runtime smoke matrix after stage 11
- **THEN** the matrix MUST include at least one narrow scheduler semantics case that exercises time slice expiry and timer-driven reschedule-on-IRQ-return
- **AND** it MUST list the xmake switches, expected serial markers, case-specific timeout, generated log paths, and required emulator backend for the case

#### Scenario: Matrix keeps unrelated smokes default-off
- **WHEN** scheduler semantics smoke is configured
- **THEN** unrelated memory, filesystem, user program, user ELF, and broad smoke options MUST remain disabled unless explicitly required by the case
- **AND** all smoke options MUST remain default-off outside explicit `xmake f ...=y` configuration

#### Scenario: Cooperative scheduler smoke remains meaningful
- **WHEN** the runtime smoke matrix includes both cooperative scheduler and preemptive scheduler semantics cases
- **THEN** the existing cooperative marker expectations MUST remain documented
- **AND** the preemption case MUST use distinct markers or artifact fields so validation can distinguish explicit yield from timer-driven rescheduling

### Requirement: Scheduler semantics validation records IRQ risk
BigOS SHALL record executed and skipped scheduler semantics validation in the structured runtime validation artifact, including low-level IRQ/timer/context-switch residual risk.

#### Scenario: Preemption smoke passes
- **WHEN** the runner observes all expected scheduler semantics serial markers within the bounded timeout
- **THEN** the validation artifact MUST record the case as passed
- **AND** it MUST include configured switches, observed markers, serial log path, timeout, emulator backend, and whether Bochs or QEMU+Bochs cross-validation was executed

#### Scenario: Preemption smoke is skipped or blocked
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display setup, serial logging, disk image generation, or scheduler smoke prerequisites are unavailable
- **THEN** the artifact MUST mark affected scheduler semantics cases as skipped or blocked rather than passed
- **AND** it MUST record substitute source/build checks and residual scheduler/timer/IRQ behavior risk

#### Scenario: Low-level cross-validation is recommended
- **WHEN** implementation changes timer IRQ, i8259 EOI ordering, ISR assembly, interrupt dispatch, context-switch assembly, port IO assumptions, or scheduler-adjacent IRQ hooks
- **THEN** validation notes MUST recommend Bochs or QEMU+Bochs cross-validation when the local environment supports it
- **AND** if cross-validation is unavailable, the artifact MUST explain why it was skipped
