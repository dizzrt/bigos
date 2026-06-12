## ADDED Requirements

### Requirement: Stage 20 validation preserves headless behavior assertions
BigOS SHALL validate interactive console usability without making graphical display, manual keyboard input, or emulator scancode injection mandatory for every automated smoke run.

#### Scenario: Headless default boot remains observable
- **WHEN** Stage 20 changes are validated through the preferred QEMU headless serial/log path
- **THEN** validation MUST continue to assert the default userland/init behavior through existing deterministic serial/log observations
- **AND** missing expected observations MUST be recorded as failure rather than reinterpreted as success

#### Scenario: Interactive console evidence is layered
- **WHEN** graphical QEMU, Bochs, manual keyboard input, or emulator keyboard injection is available
- **THEN** validation SHOULD record evidence that prompt, typed input echo, and command output are visible on the runtime text console
- **AND** the evidence MUST identify emulator backend, display/input method, executed command, observed output, and result

#### Scenario: Manual input capability is unavailable
- **WHEN** local display, ROM, keyboard input, emulator injection, disk image generation, or cross-toolchain setup prevents interactive console validation
- **THEN** validation MUST mark the interactive portion as skipped or blocked rather than passed
- **AND** validation MUST record substitute source/build/headless checks and the remaining console-usability risk

### Requirement: Stage 20 validation does not widen runtime boundaries
BigOS SHALL keep Stage 20 validation within the current bounded userland and x86_64 Legacy BIOS runtime boundary.

#### Scenario: Existing runtime contracts are preserved
- **WHEN** interactive console validation is added or executed
- **THEN** it MUST NOT require UEFI, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, SMP, dynamic linking, full POSIX terminal support, job control, or a complete POSIX libc
- **AND** it MUST NOT change boot layout, kernel link addresses, IDT vectors, syscall vector `0x80`, CR3 switching rules, disk layout, or existing smoke marker semantics
