# UEFI Boot Blueprint

This document is the UEFI boot blueprint for BigOS. BigOS now uses the x86_64 UEFI boot backend as the default runnable boot backend within the current bounded userland baseline. The backend builds `BOOTX64.EFI`, generates an ESP/FAT image, exposes a QEMU/OVMF debug entry, and reaches the existing resident init, `/bin/sh`, and bounded `/bin/*` payload boundary. Legacy BIOS remains available as an explicit compatibility and low-level debug backend.

## Current Scope

The default BigOS boot path is now UEFI:

```text
OVMF -> ESP/FAT -> EFI/BOOT/BOOTX64.EFI -> ELF64 kernel -> kernel(BootInfoHeader*) -> bounded userland
```

The retained Legacy BIOS path is explicit rather than removed:

```text
Legacy BIOS path                         UEFI path
────────────────                         ─────────
MBR -> DBR -> exDBR -> boot.bin          BOOTX64.EFI
              │                              │
              ├─ BIOS E820                  ├─ UEFI GetMemoryMap
              ├─ VGA text                   ├─ firmware console/serial
              ├─ ATA/exFAT                  ├─ SimpleFileSystem/ESP
              │                              │
              └──────── normalize ──────────┘
                           │
                      BootInfo v2+
                           │
                        kernel()
```

Non-goals for the default UEFI backend:

- Do not change the Legacy BIOS/MBR/exFAT/Bochs semantics of `xmake run bochs` or its `--display sdl2|none` target arguments.
- Do not replace MBR, DBR, extended DBR, `boot.bin`, or the existing raw exFAT image.
- Do not claim runtime parity beyond the current bounded userland baseline.
- Do not implement Secure Boot, ACPI table handoff, UEFI Runtime Services, persistent NVRAM semantics, new SMP scope, or a second ISA.
- Do not treat GOP framebuffer metadata handoff as glyph rendering, Unicode display, framebuffer scrollback, or graphical console parity.
- Do not require the kernel to call BIOS interrupts, UEFI Boot Services, or UEFI Runtime Services.
- Do not introduce external UEFI libraries, hosted runtime, exceptions, RTTI, or other non-freestanding dependencies.

## Kernel Entry Assumptions

Both the explicit BIOS backend and the default UEFI backend must provide one unified, verifiable entry environment before entering the kernel:

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
- UEFI backend produces a v2 blob with required `core` and `memory_map` sections plus optional `storage_metadata` and `loader_metadata` sections.
- UEFI backend may also produce optional `framebuffer_metadata` and `font_asset_metadata` sections. The framebuffer section is normalized from the firmware current GOP mode and records physical base, byte size, width, height, pixels per scanline, pixel format, bytes/bits per pixel, and write/cache hints. The font section describes the ESP-loaded first boot font asset.
- Runtime `_start` saves the entry `BootInfoHeader*`, calls `_init`, then restores it as the first `kernel()` argument.
- Kernel consumer validates magic, version, size, alignment, field offsets, section offsets, section sizes, and bounds.
- Unknown non-required sections may be skipped; missing or malformed required sections fail v2 and explicitly fall back to fixed-address v1 `BootInfo`.
- Both BIOS and UEFI backends act as unified handoff producers. Kernel consumers must not depend directly on firmware-native protocols.

Still not implemented:

- Runtime parity beyond the current bounded userland baseline.
- Glyph rendering, Unicode display, framebuffer scrollback, ACPI/SMBIOS firmware table sections, and UEFI Runtime Services support.
- Secure Boot, persistent NVRAM semantics, and non-x86_64 ISA backends.

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

When a valid framebuffer metadata section is present, the framebuffer physical range is treated conservatively even if firmware also reports an overlapping `usable` range: early buddy initialization excludes that subrange from the ordinary RAM free pool, and direct-map initialization skips it so later framebuffer writers cannot rely on an ordinary-RAM alias. Kernel code that needs to write a firmware framebuffer must request a virtual address through `bigos::mm::map_device_mmio(physical_base, length, cache_policy)`; direct `phys_to_direct()` framebuffer writes are outside the allowed boundary.

Initial UEFI `GetMemoryMap` mapping direction:

- `EfiConventionalMemory` maps to `usable`.
- `EfiLoaderCode`, `EfiLoaderData`, `EfiBootServicesCode`, and `EfiBootServicesData` map to `loader` and are not admitted to the initial free page pool.
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

`xmake run qemu` and `uv run python tools/boot_debug.py run` are the default UEFI/QEMU/OVMF boot entries. `xmake run qemu-uefi` remains an explicit alias for the same backend. `xmake run qemu-legacy`, `xmake run qemu-gdb`, and `xmake run bochs` with `--display sdl2|none` are explicit Legacy BIOS/MBR/exFAT debug paths:

- Build MBR, DBR, extended DBR, `boot.bin`, and root `kernel`.
- Generate a raw exFAT disk image.
- Use QEMU with an IDE disk path for explicit Legacy local boot, headless serial-marker smoke, or GDB-stub debugging.
- Keep Bochs as a supported Legacy BIOS local debug entry for early boot and hardware-behavior cross-checking.
- Do not require a UEFI loader, ESP image, OVMF configuration, Secure Boot, GOP framebuffer, ACPI handoff, Runtime Services, or a new storage driver.

The default UEFI backend uses separate artifacts from Legacy BIOS:

- `xmake build uefi-artifacts` builds `build/bin/x86/uefi/BOOTX64.EFI`.
- The first boot-time font source asset is `assets/fonts/unifont_all-17.0.04.hex`. The Python image helper converts the bundled Unifont HEX data into the versioned glyph lookup payload `build/assets/fonts/unifont.bin` and packages it into the ESP as `/boot/fonts/unifont.bin`; the UEFI loader consumes only the ESP runtime path and performs only basic format gating.
- `xmake run qemu -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40` prepares `build/test/uefi-esp.img`, prepares `build/test/uefi-root.raw` as the current exFAT runtime root compatibility image, copies a writable OVMF vars file to `build/test/OVMF_VARS.uefi.fd`, and launches QEMU/OVMF.
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40` is the direct helper form.

UEFI artifact isolation policy:

- BIOS path continues using raw exFAT images and artifacts such as `build/test/os.raw`.
- UEFI path uses an ESP/FAT image containing `EFI/BOOT/BOOTX64.EFI`, `/boot/kernel`, `/boot/user/init.elf`, and bounded `/bin/*` payloads. Until a FAT runtime filesystem or loader-fed runtime payload exists, QEMU also attaches a separate exFAT compatibility root image as primary IDE so the current kernel VFS can reach the same bounded userland baseline.
- The ESP/FAT image also contains `/boot/fonts/unifont.bin` for framebuffer handoff metadata and the later kernel glyph lookup view. Missing or invalid font metadata or lookup validation is a documented fallback and must not block serial diagnostics, VGA text fallback, memory initialization, or bounded userland validation.
- UEFI firmware configuration, temporary directories, and emulator configuration must not overwrite Legacy BIOS artifacts used by `xmake run qemu`, `xmake run qemu-gdb`, or `xmake run bochs`.
- UEFI smoke tests primarily use QEMU + OVMF. Bochs UEFI is not required for this backend.
- Legacy BIOS continues to use the current raw exFAT image with QEMU IDE or Bochs.

UEFI local tool assumptions:

- QEMU with x86_64 OVMF code firmware and a vars template; Homebrew QEMU paths such as `edk2-x86_64-code.fd` and `edk2-i386-vars.fd` are auto-detected when present.
- Homebrew LLVM/LLD tools: `clang`, `lld-link`, `llvm-objcopy`, and `llvm-objdump`.
- `mtools` commands: `mformat`, `mmd`, `mcopy`, and `mdir`.
- Existing x86_64 cross toolchain for the kernel and user payloads.
- Apple Silicon hosts may run x86_64 QEMU through TCG, which is slower and can require longer smoke timeouts.

UEFI BootInfo metadata sections:

- In the UEFI `core` section, `boot_protocol` is `UEFI` and `exfat_data_area_lba` is zero; ESP or root storage identity is not encoded in the Legacy exFAT field.
- The optional `storage_metadata` section describes the UEFI ESP/root source and boot path.
- The optional `loader_metadata` section records diagnostic backend, loader version/build id, firmware vendor/revision, and boot file path information.
- The optional `framebuffer_metadata` section records UEFI GOP current-mode geometry and physical framebuffer bounds before `ExitBootServices`. It is parsed as an immutable optional view by the kernel; absent metadata keeps the Legacy/VGA text and serial fallback valid, while invalid metadata is ignored before any framebuffer writes. Runtime framebuffer console rendering may consume this view only after mapping the range through `bigos::mm::map_device_mmio()`.
- The optional `font_asset_metadata` section records the ESP-loaded `/boot/fonts/unifont.bin` buffer address, byte size, glyph lookup format version, glyph/cell metrics, and loader-provided flags. The UEFI loader checks the glyph lookup header magic, header size, declared byte size, format version, table offsets, and basic metrics, but it does not parse Unicode ranges, search glyph records, classify terminal cells, or write framebuffer pixels. Kernel startup validates the payload header, range table, glyph records, bitmap bounds, alignment, and width classes before exposing a read-only lookup view for later console code.
- The glyph lookup asset currently records Unifont 8x16 half-width and 16x16 full-width bitmap glyphs. Its width class is a font asset property only; it is not UTF-8 decoding, a Unicode terminal cell policy, CJK display, or double-width terminal layout. The bounded runtime framebuffer console backend may use available glyph bitmaps to render current `char` cells and a software cursor, but it does not turn the default console into a full graphical or POSIX terminal.
- Kernel startup still depends only on valid required `core` and `memory_map` sections; missing or unknown optional sections remain skippable.

## ELF64 Loading Rules

The UEFI loader implements an ELF reader suited for UEFI and does not directly reuse the current `boot.cc` implementation, which mixes ATA, exFAT, fixed low addresses, and page-table preparation. BIOS and UEFI loaders share the same ELF64 loading rule specification:

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
| 2 | Migrate memory module to unified `BootMemoryRegion` consumer while keeping BIOS fallback | Yes, BIOS E820 is normalized | unified boot handoff capability header and memory-map section draft | Allocator initialization order, usable memory misclassification | `define-unified-boot-handoff-abi` |
| 3 | Minimal UEFI loader implementing a UEFI ELF reader, only to load kernel, fill handoff, and enter `kernel()` | Yes, promoted into the default UEFI backend | unified boot handoff capability ABI, ELF64 loading rules, toolchain spike | PE/COFF build, ExitBootServices order, page-table differences | `spike-minimal-uefi-loader` |
| 4 | ESP/FAT image generation, OVMF/QEMU debug entry, and documented command | Yes, promoted into the default UEFI backend | kernel memory API capability bootable loader | Host OVMF paths, CI portability, artifact isolation | `add-uefi-boot-debug-entry` |
| 5 | GOP framebuffer metadata handoff, ACPI RSDP/SMBIOS handoff, and fuller UEFI validation policy | Partial: GOP framebuffer/font metadata handoff only | unified boot handoff capability sections, kernel memory API capability/UEFI smoke test | Framebuffer mapping, ACPI table lifecycle, runtime metadata misuse | `handoff-gop-acpi-firmware-tables` |
| 6 | Shared ELF64 loading rule specification for BIOS and UEFI, without requiring shared loader code soon | No | Current BIOS ELF loading behavior documented | Rule/implementation drift, inconsistent error handling | `document-common-elf64-loader-rules` |

UEFI default runtime parity is bounded to the current resident init, shell, and packaged user-program baseline. Future firmware parity work must keep the Legacy BIOS path available explicitly and continue using `xmake run qemu-legacy`, `xmake run qemu-gdb`, or `xmake run bochs` when BIOS/ATA/port-IO behavior needs direct validation.
