# framebuffer-boot-handoff Specification

## Purpose
TBD - created by archiving change add-framebuffer-boot-handoff. Update Purpose after archive.
## Requirements
### Requirement: Firmware framebuffer metadata handoff

BigOS SHALL represent an early linear framebuffer through a normalized BootInfo v2 framebuffer metadata section rather than requiring kernel initialization to call firmware graphics services.

#### Scenario: UEFI framebuffer metadata is available early

- **WHEN** the UEFI backend enters the kernel after obtaining a firmware graphics output framebuffer
- **THEN** the BootInfo v2 blob MUST include a framebuffer metadata section with physical base, byte size, width, height, pixels-per-scanline, bytes-per-pixel or bits-per-pixel, pixel format, and write-combining/cacheability hints when known
- **AND** the kernel MUST be able to parse this section before terminal or console backend selection

#### Scenario: Missing framebuffer section falls back

- **WHEN** the kernel receives a valid BootInfo v2 blob without a framebuffer metadata section
- **THEN** kernel initialization MUST continue using the existing serial and VGA text fallback paths
- **AND** the missing section MUST NOT prevent Legacy BIOS or non-graphics validation from reaching the current boot baseline

#### Scenario: Invalid framebuffer section is rejected

- **WHEN** a framebuffer metadata section has zero dimensions, zero stride, an unsupported pixel format, an overflowing byte range, or a payload smaller than the documented structure
- **THEN** the kernel MUST ignore or reject that framebuffer view before any framebuffer writes
- **AND** it MUST keep serial diagnostics available for the failure or fallback path

### Requirement: Framebuffer physical memory remains reserved

BigOS SHALL keep framebuffer physical memory out of the ordinary RAM allocator and require explicit device/MMIO mapping before kernel code writes to the framebuffer.

#### Scenario: Framebuffer range is excluded from usable memory

- **WHEN** early memory initialization consumes BootMemoryRegion records and framebuffer metadata from the same BootInfo v2 blob
- **THEN** the framebuffer physical range MUST NOT be added to the buddy allocator free page pool
- **AND** overlap between usable memory and framebuffer metadata MUST be handled conservatively by preserving the framebuffer range

#### Scenario: Framebuffer range can be audited

- **WHEN** validation records a UEFI framebuffer handoff
- **THEN** it MUST include enough evidence to audit framebuffer base, size, geometry, pixel format, and whether the range was treated as reserved

#### Scenario: Framebuffer writes require device mapping

- **WHEN** kernel code needs a writable virtual address for the framebuffer
- **THEN** it MUST obtain that address through an explicit device/MMIO mapping API that receives the framebuffer physical range and attributes
- **AND** framebuffer console code MUST NOT directly write through an assumed ordinary-RAM direct-map alias

### Requirement: Early font asset metadata handoff

BigOS SHALL define a versioned early font asset metadata handoff so the UEFI loader can pass an ESP-loaded boot-time font asset to later framebuffer console code.

#### Scenario: Source font asset has a stable repository path

- **WHEN** the first boot-time font asset is prepared for UEFI framebuffer handoff
- **THEN** the repository source asset MUST be stored at `assets/fonts/unifont_all-17.0.04.hex`
- **AND** the generated binary font payload MUST be written under `build/assets/fonts/unifont.bin`
- **AND** runtime consumers MUST depend on the ESP-packaged `/boot/fonts/unifont.bin` payload rather than reading the repository source path or build output path

#### Scenario: UEFI loader provides an ESP-loaded font asset

- **WHEN** the UEFI loader provides the first boot-time font asset
- **THEN** it MUST load `/boot/fonts/unifont.bin` from the ESP before `ExitBootServices`
- **AND** the BootInfo v2 blob MUST describe the loader-provided asset address, byte size, format version, glyph dimensions or cell metrics when known, and flags needed to identify loader ownership
- **AND** kernel parsing MUST validate the metadata bounds before exposing it to console code

#### Scenario: Font metadata is optional until renderer exists

- **WHEN** no framebuffer renderer consumes font assets yet
- **THEN** absence of font metadata MUST NOT block boot, memory initialization, serial diagnostics, VGA text output, or bounded userland validation
- **AND** present but invalid font metadata MUST NOT corrupt kernel memory or change Legacy BIOS fallback behavior

