## ADDED Requirements

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
