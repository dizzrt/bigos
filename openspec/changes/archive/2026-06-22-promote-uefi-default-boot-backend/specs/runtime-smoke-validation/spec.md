## ADDED Requirements

### Requirement: Runtime smoke validation covers default UEFI boot

BigOS runtime smoke validation SHALL include a default UEFI boot validation path that proves the promoted default backend reaches the current bounded userland baseline through deterministic serial evidence.

#### Scenario: Matrix identifies default UEFI boot case

- **WHEN** a developer inspects the runtime smoke validation matrix after UEFI default promotion
- **THEN** the matrix MUST include a default UEFI boot case using the normal default boot configuration
- **AND** the case MUST list required backend dependencies, expected deterministic serial evidence, timeout, generated UEFI log/artifact paths, and whether the Legacy BIOS comparison path was run, skipped, or left for a later validation task

#### Scenario: Default UEFI boot case passes

- **WHEN** the runtime smoke runner or documented helper executes the default UEFI boot case under QEMU + OVMF
- **THEN** it MUST build or select the UEFI artifacts, prepare the ESP/FAT image, launch QEMU headless with OVMF, and observe the expected default userland evidence within the bounded timeout
- **AND** it MUST record the case as passed only after the bounded userland baseline is reached

#### Scenario: Default UEFI boot case fails

- **WHEN** the default UEFI boot case exits, panics, times out, or misses the expected default userland evidence
- **THEN** the validation artifact MUST record the case as failed
- **AND** it MUST include the serial log path, expected evidence, observed evidence when any, timeout or exit status, and failed stage when known

#### Scenario: Default UEFI boot case is blocked

- **WHEN** QEMU, OVMF, mtools, LLVM/LLD, xmake, the x86_64 cross toolchain, serial logging, or required image-generation support is unavailable
- **THEN** the validation artifact MUST mark the default UEFI boot case as skipped or blocked rather than passed
- **AND** it MUST record substitute build/source checks and remaining default-backend risk

### Requirement: Runtime smoke validation distinguishes backend defaults from smoke defaults

BigOS SHALL allow the default boot backend to be UEFI while preserving the existing default-off behavior of optional runtime smoke switches.

#### Scenario: Backend default changes do not enable smoke switches

- **WHEN** BigOS is built or booted with the normal default configuration after UEFI promotion
- **THEN** UEFI MUST be the default boot backend
- **AND** optional runtime smoke switches such as memory, timer, scheduler, syscall, filesystem, user program, user ELF, writable filesystem, pipe, and userland smoke MUST remain default-off unless explicitly configured

#### Scenario: Smoke-only paths remain explicit

- **WHEN** a developer enables a default-off smoke case
- **THEN** validation MUST record the selected smoke configuration separately from the selected boot backend
- **AND** the existence of a UEFI default backend MUST NOT make smoke-only user programs part of normal boot
