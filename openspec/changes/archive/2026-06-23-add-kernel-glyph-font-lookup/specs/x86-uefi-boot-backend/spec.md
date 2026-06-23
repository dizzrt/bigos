## ADDED Requirements

### Requirement: UEFI backend loads glyph lookup font asset

The x86_64 UEFI backend SHALL load the generated glyph lookup font asset from the ESP and describe it through BootInfo v2 font asset metadata before entering the kernel. The loader MUST remain a bounded file loader and format gate; it MUST NOT execute glyph lookup or framebuffer text rendering.

#### Scenario: Glyph lookup font asset is packaged into ESP

- **WHEN** the UEFI ESP image is prepared with framebuffer handoff support
- **THEN** the generated glyph lookup payload MUST be packaged at `/boot/fonts/unifont.bin`
- **AND** the packaged file MUST be the build output derived from the bundled bitmap font source rather than the repository source file itself

#### Scenario: Loader accepts supported glyph lookup header

- **WHEN** the UEFI loader reads `/boot/fonts/unifont.bin` from the ESP
- **THEN** it MUST validate magic, header size, format version, declared byte size, glyph/cell metrics, and bounded file size before writing font asset metadata
- **AND** it MUST preserve the loaded buffer across `ExitBootServices` when metadata is marked valid

#### Scenario: Loader rejects unsupported font payload

- **WHEN** the ESP font asset is missing, too large, too small, has unsupported format version, invalid metrics, or inconsistent header fields
- **THEN** the UEFI loader MUST record an explicit diagnostic or font-unavailable fallback
- **AND** it MUST NOT corrupt required BootInfo v2 sections, framebuffer metadata, memory map records, or the default bounded userland boot path

#### Scenario: Loader does not own glyph lookup semantics

- **WHEN** the UEFI loader has accepted and preserved the font asset buffer
- **THEN** it MUST pass address, size, format version, metrics, and flags to the kernel through BootInfo v2 font asset metadata
- **AND** it MUST NOT parse Unicode codepoint ranges, search glyph records, classify terminal cells, or write framebuffer pixels
