## ADDED Requirements

### Requirement: UEFI backend produces BootInfo v2

BigOS SHALL allow the x86_64 UEFI backend to produce the same `BootInfo v2` handoff ABI used as the primary kernel startup metadata contract.

#### Scenario: UEFI handoff validates through existing parser

- **WHEN** the kernel receives a `BootInfoHeader*` from the UEFI backend
- **THEN** the existing `BootInfo v2` validation rules MUST accept the blob if header, section table, core section, and memory map section are well-formed
- **AND** the kernel MUST NOT require a UEFI-specific parser for raw firmware descriptors

#### Scenario: UEFI protocol is distinguishable

- **WHEN** a `BootInfo v2` core section was produced by the UEFI backend
- **THEN** its boot protocol field MUST identify UEFI
- **AND** kernel code that needs source-aware behavior MUST branch on the normalized boot protocol rather than probing firmware-specific memory or services

#### Scenario: Legacy fallback remains compatible

- **WHEN** the UEFI backend support is added
- **THEN** v1 fixed-address fallback and Legacy BIOS v2 production MUST remain compatible
- **AND** the UEFI backend MUST NOT change existing `BootInfo` magic, version, size, alignment, section offsets, or register-passed handoff semantics in an incompatible way

### Requirement: UEFI memory map maps to BootMemoryRegion

BigOS SHALL define a conservative UEFI memory descriptor to `BootMemoryRegion` conversion for the UEFI backend.

#### Scenario: Usable memory is explicit

- **WHEN** a UEFI descriptor is eligible for general page allocation after `ExitBootServices`
- **THEN** the loader MAY normalize it as usable
- **AND** all runtime, MMIO, ACPI, loader-owned, kernel-owned, bad, unknown, or otherwise reserved descriptors MUST be excluded from the initial free page pool

#### Scenario: Source metadata is preserved

- **WHEN** a UEFI descriptor is converted into `BootMemoryRegion`
- **THEN** the converted entry MUST preserve source type as UEFI
- **AND** it MUST retain enough source value and attributes to audit the original firmware classification during validation

#### Scenario: Kernel memory consumer remains allocation-free

- **WHEN** early memory initialization consumes the UEFI-origin `BootMemoryRegion` section
- **THEN** it MUST support traversal before slab, kmalloc, or general heap initialization
- **AND** it MUST apply the same conservative free-list admission rules used for other normalized boot memory maps

### Requirement: Optional boot metadata sections

BigOS SHALL extend the `BootInfo v2` tagged-section model with optional metadata sections for UEFI storage provenance and loader diagnostics while keeping existing required sections compatible.

#### Scenario: Optional sections do not break existing validation

- **WHEN** a `BootInfo v2` blob includes optional storage metadata or loader metadata sections
- **THEN** existing required core and memory map validation MUST still determine whether the blob is usable for kernel startup
- **AND** unknown non-required sections MUST remain skippable according to tagged-section validation rules

#### Scenario: Storage metadata replaces UEFI exFAT field overloading

- **WHEN** a boot backend needs to describe non-Legacy storage origin such as UEFI ESP
- **THEN** it MUST use an optional storage metadata section
- **AND** it MUST NOT reinterpret `BootInfoCore.exfat_data_area_lba` as a generic storage pointer or UEFI ESP identifier

#### Scenario: Loader metadata is diagnostic

- **WHEN** a boot backend emits loader metadata
- **THEN** the section MUST be optional and diagnostic
- **AND** kernel initialization MUST NOT require it to allocate memory, initialize interrupts, enter user mode, or start PID-1 init
