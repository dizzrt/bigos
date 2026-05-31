# x86 Legacy Boot Layout

BigOS currently uses the legacy BIOS path:

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel
```

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
0x0f000         exFAT directory buffer
0x10000         boot.bin load address
0x100000        kernel higher-half page-table backing area
0x1000000       kernel physical load base
0xffffffff80000000  kernel higher-half virtual base
```

`BootInfo` is defined in `include/arch/x86/boot/boot_info.h`. Boot C++ writes it
at `BIGOS_BOOT_INFO_ADDRESS` and preserves the legacy aliases while kernel
consumers migrate. Assembly constants that touch the handoff area must match the
same address map; C++ consumers use `static_assert` layout checks from the shared
header.

The protected-mode extended DBR stage reads `boot.bin` with ATA primary-master
PIO. It therefore requires BIOS boot drive `0x80`; other BIOS drive numbers halt
with the visible `U` code in VGA text memory.
