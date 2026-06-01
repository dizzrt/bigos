# x86 Legacy Boot Layout

BigOS currently uses the legacy BIOS path:

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel
```

This path remains the current runnable boot backend and the producer of the
kernel handoff data used by the existing kernel. The UEFI plan in
`docs/arch/uefi-boot-blueprint.md` treats this path as the Legacy backend of a
future unified handoff model; it does not replace the MBR/DBR/exDBR/`boot.bin`
flow or change the meaning of `make boot-debug`.

The early boot path depends on these fixed physical and virtual addresses:

```text
0x0500..0x07ff  E820 ARDS records written by extended DBR
0x0800..0x083f  legacy boot metadata aliases
0x0800          legacy E820 entry count
0x0802          BIOS boot drive
0x080c          legacy kernel memory size
0x0830          legacy exFAT data-area LBA
0x0840..0x0887  canonical BootInfo handoff structure
0x1000..0x1fff  extended DBR load area
0x2000..0x6fff  boot-stage PML4/PDPT/PD/PT setup area
0x5000..        kernel higher-half page-directory handoff area
0x7c00          BIOS-loaded MBR/DBR sector
0x9000..0x9fff  Legacy BIOS-produced BootInfo v2 handoff blob
0x0f000         exFAT directory buffer
0x10000         boot.bin load address
0x100000        kernel higher-half page-table backing area
0x1000000       kernel physical load base
0xffffffff80000000  kernel higher-half virtual base
```

`BootInfo` v1 is defined in `include/arch/x86/boot/boot_info.h`. Boot C++
continues to write it at `BIGOS_BOOT_INFO_ADDRESS` (`0x0840`) and preserves the
legacy aliases while kernel consumers migrate. Its magic, version, size, field
offsets, alignment, and fixed address remain compatibility ABI.

The primary handoff path is now `BootInfo` v2. Legacy BIOS boot C++ builds a
bounded `BootInfoHeader + BootInfoSection[]` blob at `0x9000..0x9fff`, then
`boot.s` passes its `BootInfoHeader*` in `rdi` before jumping to the kernel ELF
entry. The v2 blob address is a producer-side implementation detail for this
backend; the kernel ABI is the register-passed pointer plus relative section
offsets from the header base.

The v2 blob currently contains two required sections:

- `core`: Legacy BIOS protocol metadata, boot drive, exFAT data-area LBA, kernel
  load virtual address, kernel entry virtual address, kernel file size, and
  kernel memory size.
- `memory_map`: `BootMemoryRegion[]` entries normalized from BIOS E820 ARDS.

The v2 magic is independent from the v1 magic, so consumers do not rely only on
`version` to distinguish a fixed v1 struct from a header/section blob. Section
table and payload offsets are relative to `BootInfoHeader`, and consumers check
header size, total size, section table bounds, payload bounds, required section
presence, and payload alignment. Unknown optional sections are skipped; missing
or malformed required sections reject v2 and allow the explicit v1 fixed-address
fallback.

The v2 blob at `0x9000..0x9fff` does not move or overlap the E820 buffer, legacy
metadata aliases, v1 `BootInfo`, boot-stage page tables, kernel higher-half
page-table backing area, kernel physical load base, or higher-half virtual base.
Future fixed low addresses, page-table reservations, or handoff aliases must
update this layout and explain their compatibility with the future UEFI backend.

`BootMemoryRegion` maps BIOS E820 as follows:

- E820 type `1` maps to `usable`; only these regions enter the buddy free lists.
- E820 type `2` maps to `reserved`.
- E820 type `3` maps to `acpi_reclaim`.
- E820 type `4` maps to `acpi_nvs`.
- E820 type `5` maps to `bad_memory`.
- Unknown E820 types map conservatively to `reserved`.

Reserved, runtime, MMIO, ACPI reclaim, ACPI NVS, bad memory, and unknown memory
types are not released during early buddy initialization. `acpi_reclaim` remains
reserved until a future ACPI table lifecycle phase can prove it is safe to
reclaim.

The protected-mode extended DBR stage reads `boot.bin` with ATA primary-master
PIO. It therefore requires BIOS boot drive `0x80`; other BIOS drive numbers halt
with the visible `U` code in VGA text memory.

`make boot-debug` keeps its existing Legacy BIOS/MBR/exFAT/Bochs meaning. This
handoff change does not switch that target to `BOOTX64.EFI`, an ESP/FAT image, or
QEMU/OVMF.
