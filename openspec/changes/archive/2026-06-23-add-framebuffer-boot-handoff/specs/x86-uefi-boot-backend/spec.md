## ADDED Requirements

### Requirement: UEFI backend obtains current GOP framebuffer

The x86_64 UEFI backend SHALL obtain the firmware current linear framebuffer mode through UEFI Graphics Output Protocol before entering the BigOS kernel.

#### Scenario: GOP protocol is available

- **WHEN** the UEFI loader can locate Graphics Output Protocol and the firmware current mode exposes a usable linear framebuffer
- **THEN** it MUST record the selected framebuffer base, size, width, height, pixels-per-scanline, pixel format, and relevant attributes in BootInfo v2 framebuffer metadata
- **AND** it MUST do so before `ExitBootServices` and kernel entry

#### Scenario: GOP protocol is unavailable

- **WHEN** the UEFI loader cannot locate Graphics Output Protocol or cannot identify a usable linear framebuffer mode
- **THEN** it MUST fail explicitly before entering a framebuffer-dependent path or enter the kernel without framebuffer metadata only if the boot mode is documented as a fallback
- **AND** validation MUST record that framebuffer handoff was unavailable rather than claiming graphical console readiness

#### Scenario: Pixel format is normalized

- **WHEN** the UEFI loader observes a GOP pixel format or bitmask layout
- **THEN** it MUST map that firmware-specific format to a bounded BigOS framebuffer pixel format value
- **AND** unsupported formats MUST be rejected or marked unavailable before any kernel framebuffer consumer can write pixels

#### Scenario: GOP mode is not changed by the first handoff

- **WHEN** the UEFI loader obtains framebuffer metadata for this capability
- **THEN** it MUST accept the firmware current GOP mode instead of applying a resolution or pixel-format preference policy
- **AND** validation MUST check metadata self-consistency rather than require a fixed resolution

### Requirement: UEFI backend loads boot font asset

The x86_64 UEFI backend SHALL load the first boot-time font asset from the ESP and describe it through BootInfo v2 font asset metadata.

#### Scenario: Source font asset is packaged into ESP

- **WHEN** the UEFI ESP image is prepared for framebuffer handoff work
- **THEN** the source font asset MUST come from `assets/fonts/unifont_all-17.0.04.hex`
- **AND** the build path MUST write the generated font payload to `build/assets/fonts/unifont.bin`
- **AND** the packaging path MUST place the generated font payload at `/boot/fonts/unifont.bin` inside the ESP

#### Scenario: Font asset is loaded from ESP

- **WHEN** the UEFI loader prepares framebuffer handoff metadata
- **THEN** it MUST load `/boot/fonts/unifont.bin` from the ESP using bounded file-size and format-version checks
- **AND** it MUST write loader-provided font asset metadata before entering the kernel

#### Scenario: Font asset load fails

- **WHEN** the UEFI loader cannot find, read, allocate, preserve, or validate the ESP font asset
- **THEN** it MUST report an explicit loader diagnostic or enter the kernel without valid font metadata only through a documented fallback
- **AND** it MUST NOT corrupt BootInfo v2 required sections, memory map records, or the default bounded userland boot path

### Requirement: UEFI framebuffer handoff preserves boot baseline

The x86_64 UEFI backend SHALL add framebuffer handoff without regressing the existing kernel ELF loading, BootInfo v2 required sections, ESP payload packaging, or bounded userland baseline.

#### Scenario: Existing UEFI boot metadata remains present

- **WHEN** the UEFI loader includes framebuffer metadata
- **THEN** it MUST still include the required BootInfo v2 core and memory map sections and the existing optional storage/loader metadata when available
- **AND** it MUST continue to pass the BootInfoHeader pointer through the documented x86_64 first-argument register convention

#### Scenario: UEFI userland baseline remains observable

- **WHEN** the default UEFI backend boots with framebuffer metadata enabled
- **THEN** the existing bounded userland baseline MUST remain observable through deterministic serial evidence
- **AND** successful framebuffer metadata generation alone MUST NOT be treated as a successful default boot validation if the baseline is not reached
