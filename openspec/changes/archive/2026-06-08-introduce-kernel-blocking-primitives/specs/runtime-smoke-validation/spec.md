## ADDED Requirements

### Requirement: Runtime smoke matrix covers blocking primitives
BigOS SHALL extend the runtime smoke validation matrix with narrow blocking primitive cases that validate wait queue wakeup, timeout wait, and optional TTY blocking behavior without enabling unrelated smoke switches.

#### Scenario: Matrix lists blocking primitive cases
- **WHEN** a developer inspects the runtime smoke matrix after stage 10
- **THEN** the matrix MUST include at least one narrow blocking primitives case that exercises thread block/wakeup and timeout wait
- **AND** it MUST list the xmake switches, expected serial markers, case-specific timeout, generated log paths, and whether TTY blocking input is synthetic, manual, skipped, or blocked

#### Scenario: Blocking smoke preserves defaults
- **WHEN** BigOS is built or booted outside the matrix runner
- **THEN** blocking primitive smoke options MUST remain default-off unless explicitly configured with `xmake f ...=y`
- **AND** existing memory, timer, scheduler, syscall, filesystem, and user-mode smoke defaults MUST remain unchanged

### Requirement: Blocking validation records low-level residual risk
BigOS SHALL record executed and skipped blocking validation in the structured runtime validation artifact.

#### Scenario: Blocking smoke passes
- **WHEN** the runner observes all expected blocking primitive serial markers within the bounded timeout
- **THEN** the validation artifact MUST record the case as passed
- **AND** it MUST include the configured switches, observed markers, serial log path, timeout, emulator backend, and any cross-validation notes

#### Scenario: Blocking smoke is skipped or blocked
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display setup, serial logging, disk image generation, or keyboard input capability is unavailable
- **THEN** the artifact MUST mark affected blocking cases as skipped or blocked rather than passed
- **AND** it MUST record substitute source/build checks and residual scheduler/timer/IRQ behavior risk

#### Scenario: IRQ and timer changes keep cross-validation guidance
- **WHEN** blocking primitive implementation changes timer IRQ, keyboard IRQ, i8259 EOI boundaries, port IO, or scheduler-adjacent IRQ hooks
- **THEN** validation notes MUST recommend Bochs or QEMU+Bochs cross-validation when the local environment supports it
- **AND** if cross-validation is unavailable, the artifact MUST explain why it was skipped
