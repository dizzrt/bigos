## ADDED Requirements

### Requirement: Kernel direct map has an independent address window

BigOS SHALL define a kernel direct-map virtual address window that is independent from
`KVMEM_BASE`, the recursive self-mapping window, low identity mapping, and the higher-half
kernel image mapping. The direct-map window MUST have explicit base and length constants and
MUST NOT redefine the heap/vmalloc semantics of `KVMEM_BASE`.

#### Scenario: direct map does not overlap existing kernel windows

- **WHEN** the kernel direct-map constants are defined
- **THEN** the direct-map virtual range does not overlap `KVMEM_BASE`/`KVMEM_LEN`
- **AND** it does not overlap the recursive self-mapping window used by page-table helpers
- **AND** it does not move the higher-half kernel base at `0xffffffff80000000`

#### Scenario: KVMEM remains heap/vmalloc allocation area

- **WHEN** code or documentation describes `KVMEM_BASE`
- **THEN** it continues to describe a kernel heap/vmalloc-style allocation area
- **AND** direct-map documentation uses separate `KDIRECT_*` or equivalent constants

### Requirement: Direct-map translation helpers are range checked

BigOS SHALL expose minimal `bigos::mm` helpers for translating between covered physical
addresses and direct-map virtual addresses. These helpers MUST make the valid range explicit
and MUST NOT silently treat out-of-range, non-covered, or MMIO/device addresses as ordinary
direct-map RAM.

#### Scenario: physical address converts to direct-map address

- **WHEN** a caller converts a page-aligned physical address covered by the direct-map RAM range
- **THEN** the returned virtual address equals the direct-map base plus the physical address
- **AND** converting the returned virtual address back yields the original physical address

#### Scenario: out-of-range physical address returns failure

- **WHEN** a caller attempts to convert a physical address outside the configured direct-map length
- **THEN** the helper rejects the conversion by returning a documented failure value such as
  `nullptr`, `false`, or an invalid physical address sentinel
- **AND** it does not return a pointer inside `KVMEM_BASE`
- **AND** it does not trigger the unified early panic path for ordinary range probing

#### Scenario: direct-map helper does not claim MMIO coverage

- **WHEN** a physical address belongs to a BootInfo region that is ACPI, firmware-reserved,
  MMIO/device memory, framebuffer, or otherwise not ordinary RAM
- **THEN** the direct-map helper does not report that region as ordinary direct-map RAM
- **AND** device/MMIO mapping remains outside this capability

### Requirement: Direct-map page-table initialization is deterministic

BigOS SHALL initialize direct-map page-table entries during early memory initialization using
the current kernel page table and existing recursive self-mapping access pattern. The mapping
MUST cover only page-aligned ordinary RAM ranges selected from the normalized BootInfo memory
map, including buddy-allocatable RAM and kernel/boot-consumed ranges that are physically
ordinary RAM. It MUST NOT cover ACPI reclaim/NVS, firmware-reserved, MMIO/device, framebuffer,
or other non-ordinary-RAM ranges unless a later capability explicitly changes that policy.
Mapped leaves MUST use present, writable, supervisor-only entries unless a later capability
defines stricter attributes.

#### Scenario: BootInfo ordinary RAM ranges are mapped through direct map

- **WHEN** `init_mem()` processes normalized BootInfo ordinary RAM ranges
- **THEN** the direct-map initialization maps those physical pages at
  `direct_map_base + physical_address`
- **AND** mapped leaves are supervisor-only and present

#### Scenario: mapping skips unsupported ranges

- **WHEN** a BootInfo memory range is reserved, firmware-owned, MMIO, or outside the direct-map
  length
- **THEN** direct-map initialization does not expose it as ordinary direct-map RAM
- **AND** the skipped range does not affect `KVMEM_BASE` allocation state

#### Scenario: initialization preserves boot address assumptions

- **WHEN** direct-map initialization completes
- **THEN** `KERNEL_PML4_ADDR`, boot fixed addresses, low identity mappings required for boot,
  recursive self-mapping addresses, and the higher-half kernel mapping remain compatible with
  the existing boot handoff ABI

### Requirement: Direct-map failures do not leave unsafe partial state

BigOS SHALL handle direct-map page-table allocation or descriptor setup failures explicitly.
On failure, it MUST either roll back descriptors written by the failed initialization attempt
or stop through the unified early panic path with a stable `BIGOS_` marker. It MUST NOT continue
booting with an undocumented partial direct map.

#### Scenario: page-table page allocation failure is explicit

- **WHEN** direct-map initialization needs a new page-table page and allocation fails
- **THEN** the kernel reports the failure through the documented failure path
- **AND** it does not continue as if the direct map were fully available

#### Scenario: failed initialization does not publish invalid present entries

- **WHEN** direct-map initialization fails after writing some descriptors for the current attempt
- **THEN** the implementation clears or avoids publishing present entries that point to invalid
  physical pages
- **AND** any observable fatal path uses the unified early diagnostic mechanism

### Requirement: Direct-map validation is reproducible

BigOS SHALL provide reproducible validation for the direct-map capability. Validation MUST
include OpenSpec validation, source-level layout/API checks, the narrowest useful cross build,
and a runtime smoke path when Bochs and the cross toolchain are available.

#### Scenario: source-level validation covers layout boundaries

- **WHEN** direct-map constants and helpers are added
- **THEN** source-level tests or checks verify that the direct-map window is separate from
  `KVMEM_BASE` and that `KVMEM_BASE` is still not described as direct map

#### Scenario: runtime self-test validates controlled buddy page access

- **WHEN** `BIGOS_MM_SELF_TEST` or an equivalent gated validation path runs in an environment
  with Bochs and serial marker capture
- **THEN** the test validates reversible translation and read/write access for at least one
  controlled buddy-allocated RAM page
- **AND** it releases the page and preserves allocator accounting after validation
- **AND** the existing `BIGOS_MM_SELF_TEST_PASSED` marker remains the success oracle

#### Scenario: unavailable emulator is recorded

- **WHEN** Bochs, ROM paths, or the cross toolchain are unavailable
- **THEN** validation records which direct-map checks could not run
- **AND** it records the remaining bootability risk instead of claiming runtime coverage
