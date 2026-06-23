## ADDED Requirements

### Requirement: Font asset metadata references glyph lookup payload

BigOS SHALL keep the existing early font asset metadata handoff while requiring the ESP-loaded font payload to be a versioned glyph lookup asset after this change. The metadata MUST continue to describe loader-provided address, byte size, format version, glyph/cell metrics, and ownership flags without requiring early boot code to render glyphs.

#### Scenario: Metadata points to glyph lookup asset

- **WHEN** the UEFI loader provides a valid font asset metadata section after loading `/boot/fonts/unifont.bin`
- **THEN** the described payload MUST use the supported glyph lookup asset format
- **AND** kernel parsing MUST be able to distinguish a valid glyph lookup payload from absent, invalid, or legacy raw font payload data

#### Scenario: Handoff remains optional until renderer exists

- **WHEN** glyph lookup metadata is absent or invalid during boot
- **THEN** BigOS MUST keep existing serial diagnostics, VGA text fallback, memory initialization, and bounded userland validation available
- **AND** that fallback MUST NOT be reported as framebuffer glyph rendering or Unicode display readiness

#### Scenario: Boot handoff does not perform rendering

- **WHEN** framebuffer and font asset metadata are both present and valid
- **THEN** the handoff layer MUST expose bounded immutable metadata and glyph lookup inputs for later console code
- **AND** it MUST NOT write glyph pixels to framebuffer memory, move a software cursor, scroll a framebuffer console, or consume UTF-8 text as part of this capability
