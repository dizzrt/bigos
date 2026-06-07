# UEFI Boot Blueprint

This document is the UEFI boot blueprint for BigOS. The current stage only establishes design constraints and a project roadmap. It does not implement `BOOTX64.EFI`, does not generate ESP images, does not add a UEFI emulator smoke test, and does not change the existing Legacy BIOS boot path.

## Current Scope

The current runnable BigOS boot path remains Legacy BIOS:

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel -> kernel(BootInfoHeader*)
```

A future UEFI path should be introduced as a parallel boot backend rather than replacing the current path:

```text
Legacy BIOS path                         UEFI path
────────────────                         ─────────
MBR -> DBR -> exDBR -> boot.bin          BOOTX64.EFI
              │                              │
              ├─ BIOS E820                  ├─ UEFI GetMemoryMap
              ├─ VGA text                   ├─ GOP framebuffer
              ├─ ATA/exFAT                  ├─ SimpleFileSystem/ESP
              │                              │
              └──────── normalize ──────────┘
                           │
                      BootInfo v2+
                           │
                        kernel()
```

Non-goals for this stage:

- Do not implement `BOOTX64.EFI`.
- Do not change the Legacy BIOS/MBR/exFAT/Bochs semantics of `xmake run bochs-sdl2` or `xmake run bochs`.
- Do not replace MBR, DBR, extended DBR, `boot.bin`, or the existing raw exFAT image.
- Do not implement an ESP/FAT UEFI image, QEMU/OVMF UEFI entry, or UEFI Runtime Services.
- Do not require the kernel to call BIOS interrupts, UEFI Boot Services, or UEFI Runtime Services.
- Do not introduce external UEFI libraries, hosted runtime, exceptions, RTTI, or other non-freestanding dependencies.

## Kernel Entry Assumptions

Both the BIOS backend and future UEFI backend must provide one unified, verifiable entry environment before entering the kernel:

- Architecture target is x86_64.
- The kernel remains a higher-half ELF64 executable.
- CPU is already in long mode.
- Paging is enabled and includes kernel higher-half mappings plus required early physical/identity mappings.
- A stack is available and satisfies calling-convention alignment.
- Interrupts remain disabled; IDT and interrupt-controller state are taken over by kernel initialization.
- `BootInfo`, or a successor version, is available at kernel entry.
- The long-term kernel entry ABI passes `BootInfo*` through an agreed register, for example x86_64 System V style `rdi`.
- Fixed low-address handoff is only a Legacy BIOS migration fallback or debug compatibility mechanism.

The kernel consumer should not directly consume BIOS E820, raw UEFI descriptors, UEFI system table, or any other firmware-native protocol. Boot backends normalize firmware data into the unified handoff.

## BIOS Address-Layout Compatibility

The existing Legacy BIOS fixed address layout is unchanged in this blueprint stage. Current address constraints are recorded in `docs/en/arch/x86-boot-layout.md`; key handoff regions include:

```text
0x0500..0x07ff  E820 ARDS records written by extended DBR
0x0800..0x083f  legacy boot metadata aliases
0x0840..0x0887  canonical BootInfo handoff structure
0x2000..0x6fff  boot-stage PML4/PDPT/PD/PT setup area
0x5000..        kernel higher-half page-directory handoff area
0x9000..0x9fff  Legacy BIOS-produced BootInfo v2 handoff blob
0x100000        kernel higher-half page-table backing area
0x1000000       kernel physical load base
0xffffffff80000000  kernel higher-half virtual base
```

Any future handoff ABI or address-layout change must review these items together:

- Compatibility between `BIGOS_BOOT_INFO_ADDRESS` and legacy aliases.
- E820 buffer, boot metadata aliases, page-table reservations, and kernel load base.
- Higher-half base and ELF segment loading assumptions in `link.lds`.
- Register and stack conventions from `boot.s` to `boot.cc`, and from `boot.cc` to kernel entry.
- `BootInfo` magic, version, size, field offsets, and alignment checks.
- Whether the Legacy BIOS fallback still generates data matching the kernel consumer.

## BootInfo And Handoff Plan

The `BootInfo` v2 ABI foundation has already landed as `BootInfoHeader + tagged sections`. The fixed v1 struct remains at `BIGOS_BOOT_INFO_ADDRESS` only as a Legacy BIOS fallback. v2 uses independent magic, passes `BootInfoHeader*` through `rdi`, and describes payloads with header-relative section offsets/sizes.

Recommended long-term structure:

```text
BootInfoHeader
  magic
  version
  header_size
  total_size
  flags
  boot_protocol
  section_count
  section_table_offset

BootInfoSection[]
  type
  flags
  offset
  size
  alignment
```

Section categories to cover:

- boot protocol: identify Legacy BIOS, UEFI, or other backend, plus loader capabilities.
- unified memory map: normalized `BootMemoryRegion[]`.
- framebuffer: framebuffer information exposed by GOP or another display backend.
- firmware tables: ACPI RSDP, SMBIOS entry point, and similar firmware-table entries.
- loader metadata: loader name, version, image layout, debug flags, and reserved regions.
- ABI compatibility: legacy `BootInfo` fallback, required sections, optional sections, and unknown-section skip policy.

Implemented foundation:

- Legacy BIOS backend produces a full v2 blob with required `core` and `memory_map` sections.
- Runtime `_start` saves the entry `BootInfoHeader*`, calls `_init`, then restores it as the first `kernel()` argument.
- Kernel consumer validates magic, version, size, alignment, field offsets, section offsets, section sizes, and bounds.
- Unknown non-required sections may be skipped; missing or malformed required sections fail v2 and explicitly fall back to fixed-address v1 `BootInfo`.
- Both BIOS and future UEFI backends must act as unified handoff producers. Kernel consumers must not depend directly on firmware-native protocols.

Still not implemented:

- `BOOTX64.EFI` loader, ESP/FAT image, and QEMU/OVMF UEFI debug entry.
- GOP framebuffer, ACPI/SMBIOS firmware table sections, and loader metadata sections.
- UEFI `GetMemoryMap` producer, `ExitBootServices()` handling, and Runtime Services support.

## Unified Memory Map Plan

The memory module has moved from primary raw E820 consumer to unified `BootMemoryRegion` consumer. Legacy BIOS fallback can still temporarily normalize E820 ARDS from the stack referenced by v1 `BootInfo`.

`BootMemoryRegion` should at least express:

```text
physical_base
length
normalized_type
attributes
source_type
```

Initial normalized types:

- `usable`
- `reserved`
- `acpi_reclaim`
- `acpi_nvs`
- `mmio`
- `loader`
- `kernel`
- `bad_memory`
- `runtime`

BIOS E820 mapping:

- E820 type 1 maps to `usable`.
- E820 type 2 maps to `reserved`.
- E820 type 3 maps to `acpi_reclaim`.
- E820 type 4 maps to `acpi_nvs`.
- E820 type 5 maps to `bad_memory`.
- Unknown types conservatively map to `reserved`, preserving the original type in attributes/source metadata.

Early buddy initialization releases only `usable` regions. `reserved`, `runtime`, `mmio`, `acpi_reclaim`, `acpi_nvs`, `bad_memory`, and unknown regions never enter the buddy free list. `acpi_reclaim` stays reserved until ACPI table discovery, copying, and lifecycle management prove it safe to reclaim.

Initial UEFI `GetMemoryMap` mapping direction:

- `EfiConventionalMemory` maps to `usable`.
- `EfiLoaderCode` and `EfiLoaderData` can map to `loader` or be normalized according to the reserve policy after boot services exit.
- `EfiBootServicesCode` and `EfiBootServicesData` need UEFI loader policy after `ExitBootServices()`.
- `EfiACPIReclaimMemory` maps to `acpi_reclaim`.
- `EfiACPIMemoryNVS` maps to `acpi_nvs`.
- `EfiMemoryMappedIO` and `EfiMemoryMappedIOPortSpace` map to `mmio`.
- `EfiRuntimeServicesCode` and `EfiRuntimeServicesData` map to `runtime` and preserve `EFI_MEMORY_RUNTIME`.
- Unknown or firmware-reserved types conservatively map to `reserved`.

Near-term code does not support calling UEFI Runtime Services. Even so, the unified memory map must preserve runtime memory types and attributes, including `EFI_MEMORY_RUNTIME`, cacheability, write-back/write-combine, and similar metadata, to avoid rework when future support for UEFI variables, reset, time, or firmware diagnostics is added.

Candidate follow-up changes:

- `define-bootinfo-v2-handoff`: define `BootInfoHeader`, section table, and register handoff ABI.
- `migrate-mm-to-boot-memory-map`: migrate memory module to unified `BootMemoryRegion` consumer while preserving BIOS fallback.
- `define-firmware-memory-map-normalization`: refine mapping from E820 and UEFI descriptors into the unified memory map and validation.

## Debug Entry And Image Plan

`xmake run bochs-sdl2` is the SDL2 Legacy BIOS/MBR/exFAT/Bochs debug path, and `xmake run bochs` is the non-SDL2 fallback:

- Build MBR, DBR, extended DBR, `boot.bin`, and root `kernel`.
- Generate a raw exFAT disk image.
- Use Bochs as the Legacy BIOS local debug entry.
- Do not implicitly switch to a UEFI loader, ESP image, or OVMF configuration.

Future UEFI debug entries must use separate names and separate artifacts, for example:

- `xmake run qemu-ovmf`
- `python3 tools/uefi_boot_debug.py run`

Future UEFI artifact isolation policy:

- BIOS path continues using raw exFAT images and artifacts such as `build/test/os.raw`.
- UEFI path uses an ESP/FAT image containing `EFI/BOOT/BOOTX64.EFI` and the kernel ELF.
- UEFI firmware configuration, temporary directories, and emulator configuration must not overwrite Bochs artifacts used by `xmake run bochs-sdl2` or `xmake run bochs`.
- UEFI smoke tests should primarily use QEMU + OVMF. Bochs UEFI is optional.
- Legacy BIOS continues to use Bochs.

## ELF64 Loading Rules

A future UEFI loader should implement an ELF reader suited for UEFI and should not directly reuse the current `boot.cc` implementation, which mixes ATA, exFAT, fixed low addresses, and page-table preparation. BIOS and UEFI loaders need to share the same ELF64 loading rule specification:

- Support only ELF64 x86_64 executable kernels.
- Validate ELF header, program-header table bounds, and `PT_LOAD` segment file/memory ranges.
- Load each `PT_LOAD` segment to the expected virtual/physical mapping target.
- Zero-fill segments where `p_memsz > p_filesz`.
- Validate that `e_entry` falls inside a loaded segment.
- Reject unsupported or out-of-bounds ELF instead of trying to execute tolerantly.

## Project Roadmap

| Stage | Plan Item | Implemented Now | Recommended Prerequisites | Main Risk | OpenSpec Change Candidate |
| --- | --- | --- | --- | --- | --- |
| 1 | `BootInfoHeader + tagged sections`, register-passed `BootInfo*`, unified handoff header design and docs | Yes, Legacy BIOS producer/consumer landed | Stable current BIOS `BootInfo` layout checks | ABI breakage, inconsistent legacy fallback | `define-unified-boot-handoff-abi` |
| 2 | Migrate memory module to unified `BootMemoryRegion` consumer while keeping BIOS fallback | Yes, BIOS E820 is normalized | Stage 1 header and memory-map section draft | Allocator initialization order, usable memory misclassification | `define-unified-boot-handoff-abi` |
| 3 | Minimal UEFI loader spike implementing a UEFI ELF reader, only to load kernel, fill handoff, and enter `kernel()` | No | Stage 1 ABI, ELF64 loading rules, toolchain spike | PE/COFF build, ExitBootServices order, page-table differences | `spike-minimal-uefi-loader` |
| 4 | ESP/FAT image generation, OVMF/QEMU debug entry, and documented command | No | Stage 3 bootable loader | Host OVMF paths, CI portability, artifact isolation | `add-uefi-boot-debug-entry` |
| 5 | GOP framebuffer, ACPI RSDP/SMBIOS handoff, and fuller UEFI validation policy | No | Stage 1 sections, Stage 3/4 UEFI smoke test | Framebuffer mapping, ACPI table lifecycle, runtime metadata misuse | `handoff-gop-acpi-firmware-tables` |
| 6 | Shared ELF64 loading rule specification for BIOS and UEFI, without requiring shared loader code soon | No | Current BIOS ELF loading behavior documented | Rule/implementation drift, inconsistent error handling | `document-common-elf64-loader-rules` |

Each stage is a candidate for a later change and is not runtime implementation work for this change. Future implementation must keep the Legacy BIOS path available as fallback and continue using `xmake run bochs-sdl2` or `xmake run bochs` to validate the existing debug entry.
