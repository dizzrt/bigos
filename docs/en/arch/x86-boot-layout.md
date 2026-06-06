# x86 Legacy Boot Layout

BigOS currently uses the Legacy BIOS path:

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel
```

This path remains the currently runnable boot backend and the producer of the kernel handoff data used by the existing kernel. The UEFI plan in `docs/en/arch/uefi-boot-blueprint.md` treats it as the Legacy backend for a future unified handoff model. It does not replace the MBR/DBR/exDBR/`boot.bin` flow and does not change the meaning of `make boot-debug`.

The early boot path depends on these fixed physical and virtual addresses:

```text
0x0500..0x07ff  E820 ARDS records written by extended DBR
0x0800..0x083f  legacy boot metadata aliases
0x0800          legacy E820 entry count
0x0802          BIOS boot drive
0x080c          legacy kernel memory size
0x0830          legacy exFAT data-region LBA
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
0xffff800000000000..0xffff80ffffffffff  recursive self-mapping window
0xffff880000000000..0xffff88ffffffffff  KVMEM heap/vmalloc-style allocation window, not direct map
0xffff900000000000..0xffffcfffffffffff  kernel direct map, ordinary RAM only
0xffffffff80000000  kernel higher-half virtual base
```

`BootInfo` v1 is defined in `include/arch/x86/boot/boot_info.h`. Boot C++ continues to write it at `BIGOS_BOOT_INFO_ADDRESS` (`0x0840`) and keeps legacy aliases during kernel consumer migration. Its magic, version, size, field offsets, alignment, and fixed address are still compatibility ABI.

## Kernel ELF Segment Layout

`link.lds` keeps the higher-half kernel at `0xffffffff80000000`, keeps `_start` as the entry, and splits loadable ELF program headers into three permission classes:

- `text`: `PT_LOAD FLAGS(5)`, meaning `PF_R | PF_X`. It covers `.bigos`, `.init`, `.text`, and `.fini`, so `_start` is inside the RX segment.
- `rodata`: `PT_LOAD FLAGS(4)`, meaning `PF_R`. It covers `.rodata`, `.rodata1`, read-only `.eh_frame_hdr`, and read-only `.eh_frame`.
- `data`: `PT_LOAD FLAGS(6)`, meaning `PF_R | PF_W`. It covers `.ctors`, `.dtors`, `.data`, `.4k_area`, and `.bss`; `.4k_area` collects both the historical `.4k_area` name and the current `_section_4k_align_` `.4k_align_area` input section.

The permission-class boundaries between `text`, `rodata`, and `data` use 4 KiB alignment so future page-level permission convergence does not put executable and writable content on the same page. This change only fixes ELF program-header permissions; it does not enable runtime page-level W^X. The Legacy BIOS bootloader still builds writable higher-half page-table mappings and still passes fixed kernel load base, entry, and memory extent through BootInfo.

`.ctors` and `.dtors` remain in the RW `data` segment for this change to avoid changing legacy C++ runtime constructor/destructor table writability assumptions while fixing ELF layout. Future kernel text/rodata/data page-level permission convergence should first expose page-aligned linker boundary symbols from `link.lds`, then let kernel virtual memory initialization consume them. Directly consuming ELF `p_flags` requires a separate BootInfo/loader segment-metadata design.

Layout validation prefers:

- `xmake -r`: confirm the cross build succeeds and no longer reports `LOAD segment with RWX permissions`.
- `x86_64-elf-objdump -p build/kernel` or `x86_64-elf-readelf -l build/kernel`: confirm the three `LOAD` segments are `r-x`, `r--`, and `rw-`, with no `rwx`.
- `x86_64-elf-objdump -f build/kernel`: confirm entry point is still `0xffffffff80000000`.
- `x86_64-elf-objdump -h build/kernel`: confirm `.bigos/.init/.text/.fini`, read-only `.rodata/.eh_frame*`, and `.ctors/.dtors/.data/.4k_area/.bss` are in expected address ranges.

The primary handoff path is now `BootInfo` v2. Legacy BIOS boot C++ builds a bounded `BootInfoHeader + BootInfoSection[]` blob at `0x9000..0x9fff`, then `boot.s` passes its `BootInfoHeader*` in `rdi` before jumping to the kernel ELF entry. The v2 blob address is producer-side backend detail; the kernel ABI is the pointer passed in a register plus section offsets relative to the header base.

The v2 blob currently contains two required sections:

- `core`: Legacy BIOS protocol metadata, boot drive, exFAT data-region LBA, kernel load virtual address, kernel entry virtual address, kernel file size, and kernel memory size.
- `memory_map`: `BootMemoryRegion[]` entries normalized from BIOS E820 ARDS.

The v2 magic is independent from the v1 magic, so consumers do not distinguish the fixed v1 struct from the header/section blob using `version` alone. The section table and payload offsets are relative to `BootInfoHeader`. Consumers check header size, total size, section-table bounds, payload bounds, required sections, and payload alignment. Unknown optional sections are skipped; missing or malformed required sections reject v2 and allow explicit fallback to fixed-address v1.

The v2 blob at `0x9000..0x9fff` does not move or overlap the E820 buffer, legacy metadata aliases, v1 `BootInfo`, boot-stage page tables, kernel higher-half page-table backing area, kernel physical load base, or higher-half virtual base. Future fixed low addresses, page-table reserved regions, or handoff aliases must update this layout and describe compatibility with the future UEFI backend.

`BootMemoryRegion` maps BIOS E820 as follows:

- E820 type `1` maps to `usable`; only these regions enter buddy free lists.
- E820 type `2` maps to `reserved`.
- E820 type `3` maps to `acpi_reclaim`.
- E820 type `4` maps to `acpi_nvs`.
- E820 type `5` maps to `bad_memory`.
- Unknown E820 types conservatively map to `reserved`.

Reserved, runtime, MMIO, ACPI reclaim, ACPI NVS, bad memory, and unknown memory types are not released during early buddy initialization. `acpi_reclaim` remains reserved until a future ACPI table lifecycle stage proves it can be reclaimed safely.

The kernel direct map is created after `init_buddy()` and `init_vmem()`, before `BIGOS_MM_SELF_TEST`. It uses `KDIRECT_BASE = 0xffff900000000000` and `KDIRECT_LEN = 0x400000000000`, selects only page-aligned ordinary RAM ranges from the BootInfo memory map, and maps them as `direct = KDIRECT_BASE + physical`. `KVMEM_BASE` remains the kernel heap/vmalloc-style virtual allocation window and does not promise any linear relationship to physical addresses. Recursive self-mapping window, low identity map, higher-half kernel base, and fixed boot handoff addresses are not redefined by the direct map.

The first direct-map version does not cover MMIO, framebuffer, ACPI reclaim/NVS, firmware reserved, runtime, bad memory, or unknown types. Future device BAR, APIC, framebuffer, or cache-attribute-sensitive device memory needs a separate MMIO mapping API instead of reusing ordinary-RAM direct-map helpers.

The protected-mode extended DBR stage reads `boot.bin` through ATA primary-master PIO. Therefore it requires BIOS boot drive `0x80`; other BIOS drive numbers halt the system and display a visible `U` code in VGA text memory.

`make boot-debug` keeps the existing Legacy BIOS/MBR/exFAT/Bochs meaning. This handoff change does not switch that target to `BOOTX64.EFI`, an ESP/FAT image, or QEMU/OVMF.
