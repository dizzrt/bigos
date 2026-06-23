## Purpose

Define the runnable x86_64 UEFI boot backend for BigOS. This capability covers
the PE/COFF UEFI loader artifact, ESP/FAT image generation, QEMU/OVMF smoke
entry, BootInfo v2 handoff production, conservative UEFI memory map
normalization, optional boot metadata, and preservation of the explicit Legacy
BIOS backend while UEFI is the default runnable backend.
## Requirements
### Requirement: x86_64 UEFI loader artifact

BigOS SHALL provide an x86_64 UEFI boot backend spike that builds a PE/COFF UEFI application suitable for the removable media path `EFI/BOOT/BOOTX64.EFI`.

#### Scenario: UEFI loader is built

- **WHEN** the UEFI boot target is built on a host with the required LLVM/LLD tools
- **THEN** the build MUST produce a `BOOTX64.EFI` artifact under the build output tree
- **AND** the artifact MUST be isolated from the existing Legacy BIOS MBR/DBR/extended-DBR/`boot.bin` artifacts

#### Scenario: Missing UEFI build tool is reported

- **WHEN** `clang`, `lld-link`, or required LLVM inspection/copy tools are unavailable
- **THEN** the UEFI build MUST fail before producing a stale `BOOTX64.EFI`
- **AND** the failure MUST identify the missing tool and the UEFI build stage

### Requirement: UEFI ESP image generation

BigOS SHALL generate an independent FAT ESP image for UEFI boot testing, without reusing or overwriting the existing Legacy BIOS raw image.

#### Scenario: ESP contains removable media boot path

- **WHEN** UEFI image generation completes successfully
- **THEN** the ESP image MUST contain `EFI/BOOT/BOOTX64.EFI`
- **AND** the image MUST contain the kernel ELF and the bounded userland payload needed by the selected boot configuration

#### Scenario: ESP generation uses user-space tools

- **WHEN** the host provides `mformat`, `mmd`, `mcopy`, and `mdir`
- **THEN** ESP generation MUST use those user-space tools or an equivalent no-mount implementation
- **AND** it MUST NOT require host disk mounting, loop devices, `diskutil` attach, or privileged filesystem mutation

#### Scenario: ESP artifacts are isolated

- **WHEN** UEFI image generation runs repeatedly
- **THEN** it MUST overwrite only documented UEFI-generated outputs under the build/test output area or an explicitly requested output path
- **AND** it MUST NOT overwrite the Legacy BIOS raw image, Bochs configuration, BIOS boot sectors, or hand-written source files

### Requirement: UEFI loader loads the existing kernel ELF

The UEFI loader SHALL load the existing x86_64 kernel ELF artifact and transfer control to its validated entry point without changing the kernel link address or kernel binary format.

#### Scenario: Kernel ELF is valid

- **WHEN** the UEFI loader reads a valid x86_64 ELF64 kernel with supported loadable segments
- **THEN** it MUST load the kernel segments according to the shared BigOS ELF64 loading constraints
- **AND** it MUST jump to the validated kernel entry using the existing x86_64 kernel handoff convention

#### Scenario: Kernel ELF is invalid

- **WHEN** the UEFI loader observes an unsupported ELF class, machine type, segment layout, entry point, file bounds, or memory bounds
- **THEN** it MUST stop before entering the kernel
- **AND** it MUST report an explicit UEFI loader failure through available firmware console or serial diagnostics

### Requirement: UEFI memory map normalization

The UEFI loader SHALL convert UEFI memory descriptors into the existing `BootMemoryRegion` view before entering the kernel.

#### Scenario: UEFI memory descriptors are normalized

- **WHEN** the UEFI loader obtains the final UEFI memory map before `ExitBootServices`
- **THEN** it MUST produce a `BootMemoryRegion` array in the `BootInfo v2` memory map section
- **AND** each entry MUST preserve physical base, length, normalized type, source type, source value, and attributes needed by early memory initialization

#### Scenario: Unknown UEFI memory type is conservative

- **WHEN** a UEFI memory descriptor has a type not explicitly mapped by BigOS
- **THEN** the loader MUST map it to a conservative non-usable normalized type
- **AND** early memory initialization MUST NOT add that region to the free page pool

#### Scenario: ExitBootServices map key changes

- **WHEN** `ExitBootServices` fails because the memory map key is stale
- **THEN** the loader MUST refresh the memory map and retry in a bounded way or fail explicitly before kernel entry

### Requirement: UEFI handoff uses BootInfo v2

The UEFI loader SHALL enter the kernel with a valid `BootInfo v2` handoff blob as the primary startup metadata contract.

#### Scenario: UEFI core section is present

- **WHEN** the UEFI loader enters the kernel
- **THEN** the `BootInfo v2` blob MUST include a required core section whose boot protocol identifies UEFI
- **AND** the core section MUST include kernel load address, kernel entry address, kernel file size, and kernel memory size metadata consistent with the loaded ELF

#### Scenario: Kernel receives register-passed handoff

- **WHEN** the UEFI loader transfers control to the kernel entry
- **THEN** it MUST pass the `BootInfoHeader*` using the documented x86_64 first-argument register convention
- **AND** it MUST NOT require the kernel to read UEFI Boot Services, UEFI raw descriptors, or fixed BIOS-only handoff addresses

#### Scenario: UEFI core does not overload Legacy exFAT metadata

- **WHEN** the UEFI loader fills the `BootInfoCore` section
- **THEN** `exfat_data_area_lba` MUST be zero for the UEFI backend
- **AND** ESP, root-device, or storage-origin details MUST be represented through a dedicated optional storage metadata section rather than overloading the Legacy exFAT field

### Requirement: UEFI metadata sections

The UEFI loader SHALL provide optional `BootInfo v2` metadata sections for storage provenance and loader diagnostics without making kernel startup depend on those sections.

#### Scenario: Storage metadata section describes ESP source

- **WHEN** the UEFI loader can identify the ESP or boot file source used to load the kernel and userland payload
- **THEN** it MUST write an optional storage metadata section with normalized storage/source details
- **AND** the kernel MUST still be able to boot if that optional section is absent but the required core and memory map sections are valid

#### Scenario: Loader metadata section describes firmware and build source

- **WHEN** the UEFI loader constructs the `BootInfo v2` blob
- **THEN** it MUST include an optional loader metadata section with auditable loader information such as backend, loader build id/version, UEFI firmware vendor/revision, or ESP path information when available
- **AND** validation MUST record whether the section was present and well-formed

### Requirement: UEFI backend preserves Legacy BIOS baseline

The UEFI backend SHALL be promoted from a parallel spike to the default runnable boot backend while the Legacy BIOS backend remains available as an explicit, unchanged compatibility path.

#### Scenario: Legacy backend remains runnable

- **WHEN** the UEFI backend becomes the default runnable backend
- **THEN** the existing Legacy BIOS/MBR/exFAT QEMU and Bochs boot paths MUST remain available through explicit backend selection
- **AND** their boot protocol, disk layout, kernel entry ABI, smoke marker strings, and runtime behavior MUST remain unchanged unless a later Legacy-specific change explicitly modifies them

#### Scenario: UEFI backend is default

- **WHEN** a developer invokes the documented default boot/debug entry after UEFI promotion
- **THEN** BigOS MUST boot through the UEFI backend, generated ESP/FAT image, and OVMF-compatible loader path
- **AND** it MUST NOT silently fall back to the Legacy BIOS raw image unless the UEFI path is explicitly unavailable and the validation or command output records the fallback as such

#### Scenario: Legacy backend selection is explicit

- **WHEN** a developer requests the Legacy BIOS backend
- **THEN** BigOS MUST continue to boot through the existing Legacy BIOS backend
- **AND** it MUST NOT require UEFI, OVMF, ESP/FAT images, Secure Boot, GOP framebuffer, ACPI handoff, Runtime Services, or a new storage device model

### Requirement: UEFI smoke validation

BigOS SHALL provide a bounded default UEFI smoke validation path that uses QEMU + OVMF and serial output as the primary observable signal for the default runnable backend.

#### Scenario: UEFI smoke observes kernel progress

- **WHEN** the UEFI QEMU/OVMF smoke path runs on a host with required firmware, QEMU, and build tools
- **THEN** it MUST launch the generated ESP through x86_64 OVMF
- **AND** it MUST record serial output under a documented UEFI-specific build/test log path
- **AND** it MUST report whether the expected default init/user exec marker was observed before timeout

#### Scenario: UEFI smoke reaches default init and shell path

- **WHEN** the UEFI QEMU/OVMF smoke path runs the default boot configuration
- **THEN** it MUST package default PID-1 init and `/bin/sh`
- **AND** it MUST reach the same bounded userland baseline expected from the normal default boot path, including deterministic serial evidence for the current default init/user exec behavior

#### Scenario: UEFI smoke dependency is missing

- **WHEN** QEMU, OVMF code firmware, a writable vars copy, mtools, LLVM/LLD, or the cross-toolchain is unavailable
- **THEN** validation MUST mark the UEFI smoke as skipped or blocked rather than passed
- **AND** it MUST record the missing dependency, substitute checks, and residual boot-backend risk

### Requirement: UEFI runtime parity is bounded to current userland

The x86_64 UEFI backend SHALL define runtime parity as reaching the current BigOS bounded userland baseline, not as complete firmware, driver, POSIX, or storage parity with every future subsystem.

#### Scenario: Runtime parity excludes future firmware features

- **WHEN** documentation, OpenSpec artifacts, or validation notes describe UEFI runtime parity
- **THEN** they MUST state that parity is bounded to the current kernel and userland baseline
- **AND** they MUST NOT imply Secure Boot, GOP framebuffer console, ACPI table handoff, UEFI Runtime Services, NVRAM persistence, SMP expansion, or second-ISA support

#### Scenario: Runtime parity excludes full OS surface

- **WHEN** the UEFI backend reaches the default userland baseline
- **THEN** BigOS MUST NOT describe that result as complete POSIX process semantics, complete POSIX filesystem behavior, dynamic linking, complete libc, broad file-backed `mmap`, async I/O, or broad storage/device support

### Requirement: UEFI backend obtains current GOP framebuffer

The x86_64 UEFI backend SHALL obtain the firmware current linear framebuffer mode through UEFI Graphics Output Protocol before entering the BigOS kernel.

#### Scenario: GOP protocol is available

- **WHEN** the UEFI loader can locate Graphics Output Protocol and the firmware current mode exposes a usable linear framebuffer
- **THEN** it MUST record the selected framebuffer base, size, width, height, pixels-per-scanline, pixel format, and relevant attributes in BootInfo v2 framebuffer metadata
- **AND** it MUST do so before `ExitBootServices` and kernel entry

#### Scenario: GOP protocol is unavailable

- **WHEN** the UEFI loader cannot locate Graphics Output Protocol or cannot identify a usable linear framebuffer mode
- **THEN** it MUST fail explicitly before entering a framebuffer-dependent path or enter the kernel without framebuffer metadata only if the boot mode is documented as a fallback
- **AND** validation MUST record that framebuffer handoff was unavailable rather than claiming graphical console readiness

#### Scenario: Pixel format is normalized

- **WHEN** the UEFI loader observes a GOP pixel format or bitmask layout
- **THEN** it MUST map that firmware-specific format to a bounded BigOS framebuffer pixel format value
- **AND** unsupported formats MUST be rejected or marked unavailable before any kernel framebuffer consumer can write pixels

#### Scenario: GOP mode is not changed by the first handoff

- **WHEN** the UEFI loader obtains framebuffer metadata for this capability
- **THEN** it MUST accept the firmware current GOP mode instead of applying a resolution or pixel-format preference policy
- **AND** validation MUST check metadata self-consistency rather than require a fixed resolution

### Requirement: UEFI backend loads glyph lookup font asset

The x86_64 UEFI backend SHALL load the generated glyph lookup font asset from the ESP and describe it through BootInfo v2 font asset metadata before entering the kernel. The loader MUST remain a bounded file loader and format gate; it MUST NOT execute glyph lookup or framebuffer text rendering.

#### Scenario: Glyph lookup font asset is packaged into ESP

- **WHEN** the UEFI ESP image is prepared for framebuffer handoff work
- **THEN** the generated glyph lookup payload MUST be packaged at `/boot/fonts/unifont.bin`
- **AND** the packaged file MUST be the build output derived from the bundled bitmap font source rather than the repository source file itself

#### Scenario: Loader accepts supported glyph lookup header

- **WHEN** the UEFI loader reads `/boot/fonts/unifont.bin` from the ESP
- **THEN** it MUST validate magic, header size, format version, declared byte size, glyph/cell metrics, and bounded file size before writing font asset metadata
- **AND** it MUST preserve the loaded buffer across `ExitBootServices` when metadata is marked valid

#### Scenario: Loader rejects unsupported font payload

- **WHEN** the ESP font asset is missing, too large, too small, has unsupported format version, invalid metrics, or inconsistent header fields
- **THEN** it MUST report an explicit loader diagnostic or enter the kernel without valid font metadata only through a documented fallback
- **AND** it MUST NOT corrupt required BootInfo v2 sections, framebuffer metadata, memory map records, or the default bounded userland boot path

#### Scenario: Loader does not own glyph lookup semantics

- **WHEN** the UEFI loader has accepted and preserved the font asset buffer
- **THEN** it MUST pass address, size, format version, metrics, and flags to the kernel through BootInfo v2 font asset metadata
- **AND** it MUST NOT parse Unicode codepoint ranges, search glyph records, classify terminal cells, or write framebuffer pixels

### Requirement: UEFI framebuffer handoff preserves boot baseline

The x86_64 UEFI backend SHALL add framebuffer handoff without regressing the existing kernel ELF loading, BootInfo v2 required sections, ESP payload packaging, or bounded userland baseline.

#### Scenario: Existing UEFI boot metadata remains present

- **WHEN** the UEFI loader includes framebuffer metadata
- **THEN** it MUST still include the required BootInfo v2 core and memory map sections and the existing optional storage/loader metadata when available
- **AND** it MUST continue to pass the BootInfoHeader pointer through the documented x86_64 first-argument register convention

#### Scenario: UEFI userland baseline remains observable

- **WHEN** the default UEFI backend boots with framebuffer metadata enabled
- **THEN** the existing bounded userland baseline MUST remain observable through deterministic serial evidence
- **AND** successful framebuffer metadata generation alone MUST NOT be treated as a successful default boot validation if the baseline is not reached
