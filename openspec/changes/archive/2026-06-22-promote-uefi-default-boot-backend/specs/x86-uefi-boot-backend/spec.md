## MODIFIED Requirements

### Requirement: UEFI backend preserves Legacy BIOS baseline

The UEFI backend SHALL be promoted from a parallel spike to the default runnable boot backend while the Legacy BIOS backend remains available as an explicit, unchanged compatibility path.

#### Scenario: Legacy backend remains runnable

- **WHEN** the UEFI backend becomes the default runnable backend
- **THEN** the existing Legacy BIOS/MBR/exFAT QEMU and Bochs boot paths MUST remain available through explicit backend selection
- **AND** their boot protocol, disk layout, kernel entry ABI, smoke marker strings, and runtime behavior MUST remain unchanged unless a later Legacy-specific change explicitly modifies them

#### Scenario: UEFI backend is default

- **WHEN** a developer invokes the documented default boot/debug entry after UEFI promotion
- **THEN** BigOS MUST boot through the UEFI backend, generated ESP/FAT image, and OVMF-compatible loader path
- **AND** it MUST NOT silently fall back to the Legacy BIOS raw image unless the UEFI path is explicitly unavailable and the validation or command output records the fallback as such

#### Scenario: Legacy backend selection is explicit

- **WHEN** a developer requests the Legacy BIOS backend
- **THEN** BigOS MUST continue to boot through the existing Legacy BIOS backend
- **AND** it MUST NOT require UEFI, OVMF, ESP/FAT images, Secure Boot, GOP framebuffer, ACPI handoff, Runtime Services, or a new storage device model

### Requirement: UEFI smoke validation

BigOS SHALL provide a bounded default UEFI smoke validation path that uses QEMU + OVMF and serial output as the primary observable signal for the default runnable backend.

#### Scenario: UEFI smoke observes kernel progress

- **WHEN** the UEFI QEMU/OVMF smoke path runs on a host with required firmware, QEMU, and build tools
- **THEN** it MUST launch the generated ESP through x86_64 OVMF
- **AND** it MUST record serial output under a documented UEFI-specific build/test log path
- **AND** it MUST report whether the expected default init/user exec evidence was observed before timeout

#### Scenario: UEFI smoke reaches default init and shell path

- **WHEN** the UEFI QEMU/OVMF smoke path runs the default boot configuration
- **THEN** it MUST package default PID-1 init and `/bin/sh`
- **AND** it MUST reach the same bounded userland baseline expected from the normal default boot path, including deterministic serial evidence for the current default init/user exec behavior

#### Scenario: UEFI smoke dependency is missing

- **WHEN** QEMU, OVMF code firmware, a writable vars copy, mtools, LLVM/LLD, or the cross-toolchain is unavailable
- **THEN** validation MUST mark the UEFI smoke as skipped or blocked rather than passed
- **AND** it MUST record the missing dependency, substitute checks, and residual boot-backend risk

## ADDED Requirements

### Requirement: UEFI runtime parity is bounded to current userland

The x86_64 UEFI backend SHALL define runtime parity as reaching the current BigOS bounded userland baseline, not as complete firmware, driver, POSIX, or storage parity with every future subsystem.

#### Scenario: Runtime parity excludes future firmware features

- **WHEN** documentation, OpenSpec artifacts, or validation notes describe UEFI runtime parity
- **THEN** they MUST state that parity is bounded to the current kernel and userland baseline
- **AND** they MUST NOT imply Secure Boot, GOP framebuffer console, ACPI table handoff, UEFI Runtime Services, NVRAM persistence, SMP expansion, or second-ISA support

#### Scenario: Runtime parity excludes full OS surface

- **WHEN** the UEFI backend reaches the default userland baseline
- **THEN** BigOS MUST NOT describe that result as complete POSIX process semantics, complete POSIX filesystem behavior, dynamic linking, complete libc, broad file-backed `mmap`, async I/O, or broad storage/device support
