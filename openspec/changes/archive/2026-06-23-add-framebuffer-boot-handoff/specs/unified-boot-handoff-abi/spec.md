## ADDED Requirements

### Requirement: BootInfo v2 framebuffer and font sections

BigOS SHALL extend the BootInfo v2 tagged-section ABI with optional framebuffer and font asset metadata sections while preserving existing required core and memory map section compatibility.

#### Scenario: Optional section types are defined

- **WHEN** public boot handoff headers are built by bootloader and kernel code
- **THEN** they MUST define stable section type IDs for framebuffer metadata and font asset metadata
- **AND** they MUST provide structure size, alignment, and critical field offset checks for each new metadata payload

#### Scenario: Existing BootInfo v2 validation remains compatible

- **WHEN** a BootInfo v2 blob includes framebuffer or font metadata sections
- **THEN** required core and memory map validation MUST continue to determine whether the blob is usable for kernel startup
- **AND** older unknown optional section skip semantics MUST remain valid for non-required sections

#### Scenario: Required startup ABI is unchanged

- **WHEN** framebuffer and font metadata sections are added
- **THEN** BigOS MUST NOT change BootInfo v2 magic, version, header layout, register-passed handoff pointer semantics, kernel entry address, or v1 fixed-address fallback compatibility unless a separate ABI-breaking change explicitly specifies it

### Requirement: Handoff parser exposes optional metadata safely

BigOS SHALL parse framebuffer and font metadata as optional views with explicit validity checks instead of exposing raw unchecked section pointers to general kernel code.

#### Scenario: Parser returns a valid optional framebuffer view

- **WHEN** the BootInfo v2 parser finds a well-formed framebuffer metadata section
- **THEN** it MUST expose a bounded immutable view containing geometry, pixel format, physical range, stride, and attributes
- **AND** consumers MUST be able to distinguish valid, absent, and invalid metadata states

#### Scenario: Parser returns a valid optional font view

- **WHEN** the BootInfo v2 parser finds a well-formed font asset metadata section
- **THEN** it MUST expose a bounded immutable view containing asset location, byte size, format version, metrics, and flags
- **AND** consumers MUST NOT dereference the asset address until the bounds and mapping assumptions are valid for the current boot phase
