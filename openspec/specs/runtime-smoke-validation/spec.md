## Purpose

Define productized runtime smoke validation for BigOS, including an explicit stage 9 smoke matrix, QEMU headless marker checks, structured validation artifacts, and scenario-specific low-level cross-validation guidance.

## Requirements

### Requirement: Runtime smoke matrix is explicit

BigOS SHALL provide an explicit runtime smoke validation matrix that lists each supported stage 9 smoke case, the xmake switches required for that case, the preferred emulator path, the expected serial markers, the case-specific timeout, and the generated log or artifact paths.

#### Scenario: Matrix lists narrow smoke cases

- **WHEN** a developer inspects the stage 9 runtime smoke matrix
- **THEN** the matrix MUST include narrow cases for memory self-test, timer IRQ, scheduler, syscall, read-only filesystem, first user program, and filesystem-backed user ELF validation
- **AND** each case MUST list only the smoke switches needed for that case instead of enabling every smoke switch at once
- **AND** filesystem and filesystem-backed user ELF cases MUST be able to use longer default timeouts than fast memory, timer, scheduler, or syscall cases

#### Scenario: Matrix preserves smoke defaults

- **WHEN** BigOS is built or booted outside the matrix runner
- **THEN** all runtime smoke options MUST remain default-off unless the developer explicitly configures them with `xmake f ...=y`

#### Scenario: User-mode smoke boundaries are visible

- **WHEN** the matrix lists `user_program_smoke` or `user_elf_smoke`
- **THEN** it MUST identify that these cases compile `src/kernel/proc/**` and are not part of a normal boot configuration

### Requirement: Runtime smoke runner uses QEMU headless marker checks

BigOS SHALL provide a validation runner as a `tools/boot_debug.py` subcommand or equivalent documented helper flow that executes matrix cases through the existing Legacy BIOS/MBR/exFAT image path and prefers QEMU headless serial-marker checks for automated validation.

#### Scenario: Runner executes a matrix case

- **WHEN** the runner executes a runtime smoke matrix case
- **THEN** it MUST configure the case-specific smoke switches through `xmake f`
- **AND** it MUST build the configured kernel and boot artifacts through the xmake-backed flow
- **AND** it MUST launch the QEMU backend with headless display mode or an equivalent `tools/boot_debug.py` QEMU headless helper path
- **AND** it MUST wait for the case's expected serial marker within the case-specific bounded timeout

#### Scenario: Expected marker is observed

- **WHEN** the runner observes the expected serial marker for a case
- **THEN** it MUST mark that case as passed
- **AND** it MUST record the serial log path and observed marker in the validation artifact

#### Scenario: Expected marker is missing

- **WHEN** the runner does not observe the expected serial marker before timeout or emulator exit
- **THEN** it MUST mark that case as failed
- **AND** it MUST record the missing marker, serial log path, timeout or exit status, and failed stage

### Requirement: Validation artifact records executed and skipped checks

BigOS SHALL generate a Markdown-first structured validation artifact for the runtime smoke matrix that records the environment, matrix case results, logs, skipped checks, and residual risk in a reviewable format with JSON schema compatible fields.

#### Scenario: Validation artifact is generated

- **WHEN** a runtime smoke matrix run completes or stops after a case failure
- **THEN** the runner MUST write a validation artifact under `build/test/` or an explicitly specified output path
- **AND** the artifact MUST include tool availability, xmake configuration for each case, expected markers, observed markers, serial log paths, case-specific timeout, case status, and residual risk notes

#### Scenario: Required tool is unavailable

- **WHEN** `uv`, `xmake`, a required `x86_64-elf-*` tool, QEMU, Bochs, ROM/display configuration, or another required local dependency is unavailable
- **THEN** the artifact MUST mark affected cases as skipped or blocked rather than passed
- **AND** it MUST record the missing dependency, alternative checks that were executed, and remaining validation risk

#### Scenario: Manual validation is recorded

- **WHEN** a developer performs a documented single-case smoke manually instead of using the matrix runner
- **THEN** the validation artifact or review notes MUST record the command, smoke switches, expected marker, serial log path, result, and any skipped matrix cases

### Requirement: Low-level cross-validation is scenario-specific

BigOS SHALL keep Bochs or QEMU+Bochs cross-validation as a scenario-specific recommendation for boot, IRQ, timer, ATA PIO, port IO, and hardware-behavior changes, without making Bochs mandatory for every automated smoke case.

#### Scenario: High-risk low-level change uses cross-validation when available

- **WHEN** a change affects boot, real-mode/protected-mode/long-mode transition, interrupt dispatch, timer IRQ, ATA PIO, port IO, or low-level driver behavior
- **THEN** validation MUST include Bochs or QEMU+Bochs cross-validation when the local environment supports it
- **AND** the validation artifact MUST record the emulator backend, display mode, serial log or diagnostic output, and result

#### Scenario: Bochs cross-validation is unavailable

- **WHEN** Bochs, ROM paths, display configuration, or host emulator setup prevents cross-validation
- **THEN** the validation artifact MUST record why Bochs validation was skipped
- **AND** it MUST identify which QEMU, build, source-level, or manual checks were used as substitutes and what residual hardware-behavior risk remains

### Requirement: Runtime smoke productization preserves kernel contracts

Runtime smoke validation productization SHALL NOT change existing boot layout, kernel entry contracts, interrupt/syscall ABI, disk layout, smoke marker strings, or smoke-only user process boundaries.

#### Scenario: Legacy boot path is preserved

- **WHEN** the matrix runner prepares and boots a smoke case
- **THEN** the image MUST continue to use the existing Legacy BIOS/MBR/exFAT raw image path with `/boot/boot.bin`, root `kernel`, and IDE-compatible disk exposure
- **AND** it MUST NOT require UEFI, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, or a new storage driver

#### Scenario: Runtime ABI is preserved

- **WHEN** this change adds validation tooling or documentation
- **THEN** it MUST NOT change kernel link addresses, BootInfo/handoff ABI, page-table layout assumptions, IDT vectors, IRQ EOI rules, syscall vector `0x80`, CR3 switching rules, or existing smoke marker strings

#### Scenario: Smoke failures remain observable

- **WHEN** a smoke case fails in the kernel or during boot
- **THEN** the existing COM1/VGA marker and panic behavior MUST remain the source of truth for the runner
- **AND** the runner MUST NOT reinterpret a missing marker as success

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
