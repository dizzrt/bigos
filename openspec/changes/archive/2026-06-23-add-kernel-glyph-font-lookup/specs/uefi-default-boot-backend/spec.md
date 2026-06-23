## ADDED Requirements

### Requirement: Default UEFI validation records glyph lookup asset readiness

BigOS SHALL record glyph lookup font asset readiness as part of default UEFI validation without redefining default runtime parity as framebuffer console or Unicode text parity.

#### Scenario: Glyph lookup asset is ready during default UEFI smoke

- **WHEN** default UEFI validation runs with the required build, image, QEMU, OVMF, ESP, and serial-log dependencies
- **THEN** validation MUST record whether the glyph lookup font asset was generated, packaged into ESP, loaded by the UEFI backend, and accepted or rejected by kernel-side font validation
- **AND** the default UEFI pass condition MUST still require reaching the current bounded userland baseline through deterministic serial evidence

#### Scenario: Glyph lookup asset failure is distinct from default boot failure

- **WHEN** default UEFI validation reaches the bounded userland baseline but glyph lookup asset generation, packaging, loading, or kernel validation fails
- **THEN** validation MUST distinguish glyph lookup readiness failure from default boot failure
- **AND** residual risk MUST state that framebuffer glyph rendering readiness is not proven

#### Scenario: Documentation avoids overclaiming text parity

- **WHEN** documentation or validation notes describe glyph lookup readiness
- **THEN** they MUST describe it as a font asset lookup input for later framebuffer console work
- **AND** they MUST NOT claim completed framebuffer glyph rendering, Unicode display, software cursor support, framebuffer scrollback, Secure Boot, ACPI handoff, UEFI Runtime Services, or full device/storage parity
