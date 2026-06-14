## ADDED Requirements

### Requirement: UEFI QEMU/OVMF debug entry

BigOS SHALL provide an explicit UEFI QEMU/OVMF local debug entry that builds UEFI artifacts, prepares an ESP/FAT image, and launches QEMU with x86_64 OVMF firmware.

#### Scenario: Developer starts UEFI debug entry

- **WHEN** a developer invokes the documented UEFI debug entry from the repository root
- **THEN** the command MUST build the configured kernel, user payloads, and `BOOTX64.EFI`
- **AND** it MUST generate or refresh the UEFI ESP image
- **AND** it MUST launch `qemu-system-x86_64` with x86_64 OVMF firmware and the generated ESP image

#### Scenario: UEFI debug entry uses headless serial mode

- **WHEN** the UEFI debug entry is run in headless mode
- **THEN** it MUST configure QEMU without requiring an interactive display
- **AND** it MUST write COM1 serial output to a documented UEFI-specific log path under the build/test output area unless explicitly overridden

#### Scenario: UEFI debug entry validates default runtime path

- **WHEN** the UEFI debug entry runs the default boot configuration with marker validation enabled
- **THEN** it MUST wait for the same default init/user exec marker used by the Legacy BIOS default headless path
- **AND** missing that marker, including the current `BIGOS_USER_EXEC` baseline marker, MUST be reported as a failed or blocked UEFI runtime-parity check rather than success

#### Scenario: OVMF vars are writable copy

- **WHEN** the UEFI debug entry prepares QEMU firmware inputs
- **THEN** it MUST use the OVMF code firmware as read-only input
- **AND** it MUST copy the OVMF vars template to a generated writable output path before launch
- **AND** it MUST NOT mutate the package-manager-installed vars template in place

### Requirement: UEFI debug preflight

The UEFI debug entry SHALL validate UEFI-specific local dependencies before mutating generated images or launching QEMU.

#### Scenario: OVMF code firmware is missing

- **WHEN** the configured or auto-detected x86_64 OVMF code firmware is unavailable
- **THEN** the UEFI debug entry MUST stop before QEMU launch
- **AND** it MUST report the missing firmware path and the UEFI preflight stage

#### Scenario: OVMF vars template is missing

- **WHEN** the configured or auto-detected OVMF vars template is unavailable
- **THEN** the UEFI debug entry MUST stop before QEMU launch
- **AND** it MUST report the missing vars template and the UEFI preflight stage

#### Scenario: ESP tooling is missing

- **WHEN** required FAT image tools are unavailable
- **THEN** the UEFI debug entry MUST stop before ESP image generation
- **AND** it MUST report the missing tool and the ESP generation stage

### Requirement: Legacy and UEFI debug outputs remain isolated

BigOS SHALL keep UEFI debug outputs separate from existing Legacy BIOS debug outputs.

#### Scenario: Existing Legacy QEMU entry remains BIOS

- **WHEN** a developer invokes the existing QEMU Legacy BIOS entry
- **THEN** it MUST continue to generate and launch the Legacy BIOS/MBR/exFAT raw image
- **AND** it MUST NOT use OVMF, ESP/FAT images, `BOOTX64.EFI`, or UEFI-specific vars files

#### Scenario: Existing Bochs entry remains BIOS

- **WHEN** a developer invokes the existing Bochs entry
- **THEN** it MUST continue to generate and launch the Legacy BIOS/MBR/exFAT raw image
- **AND** it MUST NOT depend on OVMF, ESP/FAT images, or the UEFI loader artifact

#### Scenario: Artifact paths are distinct

- **WHEN** both Legacy BIOS and UEFI debug entries are used in the same workspace
- **THEN** generated raw images, ESP images, firmware vars copies, emulator configs, and serial logs MUST use distinct documented paths
- **AND** rerunning one backend MUST NOT silently delete or overwrite the other backend's generated outputs except through documented cleanup behavior
