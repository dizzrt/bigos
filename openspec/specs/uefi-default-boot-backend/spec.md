## Purpose

Define the promoted x86_64 UEFI default boot backend for BigOS. This capability
covers default backend selection, explicit Legacy BIOS preservation, bounded
userland payload parity, kernel ABI and memory assumptions, and validation
outcome reporting for local dependency failures.
## Requirements
### Requirement: UEFI default backend selection

BigOS SHALL use the x86_64 UEFI backend as the default runnable boot backend while preserving an explicit Legacy BIOS backend selection path.

#### Scenario: Default boot uses UEFI

- **WHEN** a developer invokes the documented default boot/run path after this change
- **THEN** BigOS MUST build or select the UEFI boot artifacts needed for the default run
- **AND** it MUST launch through the UEFI loader and ESP/FAT image path rather than silently using the Legacy BIOS MBR/exFAT path

#### Scenario: Legacy BIOS remains explicit

- **WHEN** a developer selects the Legacy BIOS backend explicitly
- **THEN** BigOS MUST keep the existing MBR/DBR/extended-DBR/`boot.bin` backend available
- **AND** it MUST NOT require OVMF, ESP/FAT images, Secure Boot, GOP framebuffer, ACPI handoff, or a new storage driver for that explicit Legacy BIOS path

### Requirement: UEFI default backend reaches bounded userland

The default UEFI boot backend SHALL reach the existing bounded userland baseline using the same kernel/user payload boundary as the current normal boot configuration.

#### Scenario: Default UEFI image contains bounded userland payload

- **WHEN** the default UEFI boot image is prepared
- **THEN** the ESP/FAT image MUST include the kernel ELF, resident PID-1 init payload, `/bin/sh`, and the default bounded `/bin/*` programs needed by normal boot
- **AND** smoke-only replacement programs MUST remain selected only by explicit default-off smoke configuration

#### Scenario: Default UEFI boot reaches user-visible baseline

- **WHEN** the default UEFI backend boots under the preferred QEMU + OVMF headless validation path
- **THEN** validation MUST observe deterministic serial evidence that the default init/shell/user exec behavior reached the current bounded userland baseline
- **AND** entering the kernel without reaching that userland baseline MUST NOT be recorded as a passed default backend validation

### Requirement: Default UEFI backend preserves kernel ABI and memory assumptions

Promoting UEFI to the default backend SHALL NOT silently change kernel link addresses, kernel entry ABI, `BootInfo v2` layout assumptions, page-table layout, IDT vectors, syscall vector, CR3 switching rules, or existing bounded userland ABI.

#### Scenario: Boot ABI review is required

- **WHEN** implementation promotes UEFI to the default backend
- **THEN** review or validation notes MUST record whether kernel link address, entry register convention, `BootInfo` magic/version/size/alignment, page-table assumptions, IDT vectors, and syscall vector changed
- **AND** any intended change to those assumptions MUST be explicitly specified rather than bundled into backend default selection

#### Scenario: UEFI memory map remains conservative

- **WHEN** the default UEFI loader passes memory information to the kernel
- **THEN** it MUST pass normalized `BootMemoryRegion` records through `BootInfo v2`
- **AND** unknown, runtime, MMIO, ACPI, bad, firmware-reserved, or otherwise unsafe UEFI memory descriptors MUST NOT be added to the initial free page pool

### Requirement: Default UEFI backend dependency failures are explicit

UEFI default boot validation SHALL distinguish passed, failed, skipped, and blocked outcomes based on local tool and emulator availability.

#### Scenario: Required UEFI dependency is missing

- **WHEN** QEMU, OVMF code firmware, writable OVMF vars template, mtools, LLVM/LLD, xmake, the x86_64 cross toolchain, or serial-log support is unavailable
- **THEN** affected UEFI validation MUST be marked skipped or blocked rather than passed
- **AND** validation notes MUST record the missing dependency, substitute checks, and remaining default-backend bootability risk

#### Scenario: UEFI boot fails before userland baseline

- **WHEN** the UEFI backend exits, panics, times out, or misses the expected userland evidence before reaching the bounded userland baseline
- **THEN** validation MUST mark the default UEFI backend check as failed
- **AND** it MUST record the serial log path, timeout or exit status, failed stage when known, and residual risk

### Requirement: Default UEFI validation records framebuffer handoff

BigOS SHALL record framebuffer handoff status as part of default UEFI validation without redefining default runtime parity as graphical console parity.

#### Scenario: Framebuffer handoff succeeds during default UEFI smoke

- **WHEN** default UEFI validation runs on a host with the required QEMU, OVMF, build, image, and serial-log dependencies
- **THEN** validation MUST record whether framebuffer metadata was produced, parsed, and kept out of the ordinary RAM allocator
- **AND** the validation pass condition MUST still require reaching the current bounded userland baseline through deterministic serial evidence

#### Scenario: Framebuffer handoff is unavailable but boot fallback is valid

- **WHEN** default UEFI validation reaches the bounded userland baseline but cannot obtain or parse framebuffer metadata
- **THEN** the result MUST distinguish framebuffer-handoff failure from default boot failure
- **AND** the residual risk MUST state that graphical framebuffer console readiness is not proven

### Requirement: Legacy backend remains independent of framebuffer handoff

BigOS SHALL keep explicit Legacy BIOS backend validation independent from UEFI GOP framebuffer requirements.

#### Scenario: Legacy backend is selected explicitly

- **WHEN** a developer selects the Legacy BIOS backend after framebuffer handoff support is added
- **THEN** the boot path MUST NOT require UEFI GOP, OVMF, ESP framebuffer metadata, font metadata, or graphical console support
- **AND** it MUST continue to use the existing Legacy BIOS text/serial diagnostic behavior unless a separate Legacy-specific change modifies it

#### Scenario: Documentation avoids overclaiming graphical parity

- **WHEN** documentation or validation notes describe the default UEFI backend after framebuffer handoff support
- **THEN** they MUST describe framebuffer metadata handoff as an input for later framebuffer console work
- **AND** they MUST NOT claim completed glyph rendering, Unicode display, framebuffer scrollback, Secure Boot, ACPI handoff, UEFI Runtime Services, or full device/storage parity

