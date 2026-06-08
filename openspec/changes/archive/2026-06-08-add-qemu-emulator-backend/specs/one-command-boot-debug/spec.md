## ADDED Requirements

### Requirement: QEMU launch for Legacy BIOS boot debugging

BigOS SHALL provide QEMU-based xmake boot debug entries that launch the same generated Legacy BIOS/MBR/exFAT raw image used by the Bochs workflow, without changing bootloader, kernel handoff, disk image layout, or kernel runtime initialization semantics.

#### Scenario: QEMU local boot entry is available

- **WHEN** a developer runs `xmake run qemu` from the repository root
- **THEN** the command SHALL build the configured kernel and boot artifacts through xmake
- **AND** it SHALL generate or refresh the deterministic raw boot image through the Python helper
- **AND** it SHALL launch `qemu-system-x86_64` against that raw image
- **AND** it SHALL write COM1 serial output to a documented build output log path

#### Scenario: QEMU uses the existing ATA PIO compatible disk path

- **WHEN** the QEMU backend launches the generated raw image
- **THEN** it MUST expose the image through a Legacy BIOS compatible IDE disk configuration
- **AND** it MUST NOT require virtio, AHCI/SATA, NVMe, UEFI, OVMF, or a new kernel storage driver

#### Scenario: QEMU keeps Legacy BIOS runtime semantics

- **WHEN** the QEMU backend launches BigOS
- **THEN** the boot path MUST continue to use the generated MBR, DBR, extended DBR, `/boot/boot.bin`, and root `kernel`
- **AND** it MUST NOT change the kernel link address, boot handoff ABI, BootInfo location, smoke marker ABI, or kernel initialization order

### Requirement: QEMU display mode selection

BigOS SHALL provide QEMU display mode selection through the Python helper so the same QEMU backend can run with a graphical display for local inspection or without an interactive display for automated smoke tests, serial-marker checks, and CI-like validation.

#### Scenario: QEMU graphical mode is available

- **WHEN** a developer runs `xmake run qemu` without selecting headless display mode
- **THEN** the command SHALL prepare the same bootable raw image as the Bochs workflow
- **AND** it SHALL launch QEMU with graphical output suitable for observing VGA text output
- **AND** it SHALL write COM1 serial output to a documented `build/test` log path

#### Scenario: QEMU headless display mode is available

- **WHEN** the Python helper is invoked for the QEMU backend with `--display none` or an equivalent headless display parameter
- **THEN** the helper SHALL prepare the same bootable raw image as the Bochs workflow
- **AND** it SHALL launch QEMU without requiring an interactive display
- **AND** it SHALL write COM1 serial output to a documented `build/test` log path

#### Scenario: Serial marker is observed under QEMU

- **WHEN** the Python helper is invoked with QEMU backend, headless display mode, `--serial-log`, and `--expect-serial-marker`
- **THEN** the helper SHALL monitor the serial log for the expected marker
- **AND** it SHALL terminate the QEMU process group after the marker is observed
- **AND** it SHALL report success without requiring manual emulator shutdown

#### Scenario: Serial marker is not observed under QEMU

- **WHEN** the expected marker is not observed before the configured smoke timeout
- **THEN** the helper SHALL terminate the QEMU process group
- **AND** it SHALL fail with a message that identifies the missing marker, serial log path, and QEMU smoke stage

### Requirement: QEMU GDB debug entry

BigOS SHALL provide a QEMU GDB-oriented debug entry that starts the generated Legacy BIOS image paused with a QEMU GDB stub.

#### Scenario: GDB debug command is available

- **WHEN** a developer runs `xmake run qemu-gdb`
- **THEN** the command SHALL prepare the same bootable raw image as the other boot debug entries
- **AND** it SHALL launch QEMU with a GDB stub enabled
- **AND** it SHALL pause CPU execution before boot continues
- **AND** it SHALL use graphical output by default so VGA text output remains visible during GDB debugging
- **AND** it SHALL report the expected GDB attachment target or document the default QEMU GDB port

#### Scenario: GDB entry is not used for automatic smoke

- **WHEN** documentation describes smoke-test commands
- **THEN** it SHALL identify QEMU backend with headless display mode as the preferred automated QEMU smoke path
- **AND** it SHALL NOT describe `xmake run qemu-gdb` as a non-interactive smoke command

### Requirement: Emulator backend selection is explicit

The Python boot debug helper SHALL make emulator selection explicit and SHALL only require the external emulator tool needed by the selected backend.

#### Scenario: QEMU backend is selected

- **WHEN** the helper is invoked for a QEMU backend
- **THEN** preflight validation SHALL require `qemu-system-x86_64`
- **AND** it SHALL NOT require `bochs` unless a Bochs backend is selected

#### Scenario: Bochs backend is selected

- **WHEN** the helper is invoked for a Bochs backend
- **THEN** preflight validation SHALL require `bochs`
- **AND** it SHALL NOT require `qemu-system-x86_64` unless a QEMU backend is selected

#### Scenario: No-launch image validation is requested

- **WHEN** the helper is invoked with no emulator launch requested
- **THEN** preflight validation SHALL NOT require Bochs or QEMU
- **AND** raw image generation and validation SHALL remain available without an emulator installed

### Requirement: Emulator documentation remains scenario-specific

BigOS documentation SHALL describe QEMU and Bochs as complementary local debug backends with distinct recommended use cases.

#### Scenario: Documentation describes QEMU use cases

- **WHEN** documentation describes local boot validation and smoke testing
- **THEN** it SHALL identify QEMU backend with headless display mode as the preferred automated smoke and serial-marker path
- **AND** it SHALL identify `xmake run qemu` with graphical display as the preferred quick local QEMU boot validation command
- **AND** it SHALL identify `xmake run qemu-gdb` as the QEMU GDB stub debug command

#### Scenario: Documentation preserves Bochs use cases

- **WHEN** documentation describes early boot, BIOS, real-mode, protected-mode, long-mode transition, ATA PIO, interrupt, or hardware behavior investigations
- **THEN** it SHALL continue to identify `xmake run bochs-sdl2` and `xmake run bochs` as supported Bochs debug entries
- **AND** it SHALL recommend Bochs or dual-emulator cross-checking for high-risk low-level changes when the environment supports it

#### Scenario: Documentation separates QEMU Legacy BIOS from future UEFI

- **WHEN** documentation describes the QEMU backend introduced by this change
- **THEN** it MUST describe the backend as a Legacy BIOS/MBR/exFAT QEMU path
- **AND** it MUST NOT imply that UEFI loader, ESP/FAT image generation, or OVMF boot is implemented by this change

## MODIFIED Requirements

### Requirement: Preflight validation

The boot debug command SHALL validate required local tools and inputs before mutating generated images or launching the selected emulator.

#### Scenario: Required tool is missing

- **WHEN** `xmake`, `python3`, any required `x86_64-elf-*` tool, or the emulator required by the selected backend is unavailable
- **THEN** the command SHALL stop before image generation or emulator launch as appropriate
- **AND** it SHALL report the missing dependency and failed stage

#### Scenario: Build output is unavailable

- **WHEN** the kernel or boot build fails to produce a required artifact
- **THEN** the command SHALL stop before emulator launch and report which artifact is missing

### Requirement: Generated artifacts remain isolated

The boot debug workflow SHALL place generated images, emulator configs, serial logs, and temporary files under build or test output paths so source files and hand-written OpenSpec artifacts are not overwritten.

#### Scenario: Command regenerates boot debug artifacts

- **WHEN** the boot debug command is run repeatedly
- **THEN** it SHALL overwrite only documented generated outputs or an explicitly specified image path

#### Scenario: Developer wants to inspect artifacts

- **WHEN** the command completes image generation
- **THEN** it SHALL report the generated raw image path and the selected emulator's generated configuration or launch-relevant output paths

#### Scenario: QEMU serial log is generated

- **WHEN** a QEMU backend is launched with serial logging enabled
- **THEN** the serial log SHALL be written under a documented `build/test` path unless the developer explicitly specifies another path
