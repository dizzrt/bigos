# BigOS

Language: English | [简体中文](README-zh.md)

BigOS is an early-stage x86_64 operating system kernel written mainly in
freestanding C++17, C17, and assembly. It currently focuses on bootstrapping,
basic text output, interrupt setup, keyboard input handling, and early kernel
memory management.

This repository is a research/toy OS kernel project, not a hosted application or
service.

## Status

The project is in kernel infrastructure bring-up stage.

Implemented or partially implemented:

- x86 boot path with MBR, exFAT DBR, extended DBR, and long-mode transition.
- ELF64 kernel loading from an exFAT disk image.
- Higher-half kernel linking at `0xffffffff80000000`.
- VGA text-mode output and simple `kprintf` support.
- IDT setup and assembly interrupt stubs.
- i8259 PIC driver and keyboard scan-code parser.
- Buddy-based physical page allocation.
- Slab/kmalloc allocator and C++ `new`/`delete` integration.
- Early virtual-memory allocation and page-table mapping framework.
- Small KTL support library for kernel containers and helpers.

Not implemented or still skeletal:

- TTY and console abstraction beyond basic VGA output.
- Scheduler, threads, processes, and user mode.
- System calls.
- Filesystem services inside the kernel.
- Broad device-driver support.
- Complete build/install automation.

## Repository Layout

```text
.
|-- cpp               kernel C++ support library, KTL, libsupc++ subset
|-- include           public kernel headers and small libc-style header subset
|-- src               implementation sources for boot, kernel, drivers, mm, runtime
|   |-- arch/x86/boot x86 boot code, MBR/DBR, and ELF loader
|   |-- drivers       hardware drivers such as VGA and i8259 PIC
|   |-- kernel        kernel entry, IRQ, IO, string, console/TTY skeletons
|   |-- mm            buddy, slab, kmalloc, and virtual memory code
|   `-- runtime       runtime startup assembly source objects
|-- tools             developer helpers such as the boot disk install tool
|-- openspec          OpenSpec project configuration
|-- tests             validation tests and local test assets
|-- link.lds          kernel linker script
|-- toolchains.lua    xmake cross-toolchain definition
|-- xmake.lua         primary build configuration
`-- bigos.py          auxiliary command helper
```

## Boot Flow

```text
BIOS
  |
  v
MBR / exFAT DBR
  |
  v
boot.s
  - builds temporary GDT
  - prepares early page tables
  - enables PAE and long mode
  - switches to 64-bit execution
  |
  v
boot.cc
  - searches the exFAT root directory for a file named "kernel"
  - reads the ELF64 header and program header through ATA LBA48 PIO
  - prepares paging for the kernel image
  - loads the kernel at the higher-half virtual address
  |
  v
kernel()
  - clears VGA text screen
  - initializes memory management
  - initializes IRQ descriptors
  - enables interrupts
  - enters an idle hlt loop
```

Key files:

- `src/arch/x86/boot/boot.s`: early CPU mode switch and jump to the kernel.
- `src/arch/x86/boot/boot.cc`: disk read, exFAT lookup, ELF loading.
- `src/kernel/kernel.cc`: main kernel entry.
- `link.lds`: higher-half kernel layout.

## Build And Run

The primary build system is xmake and the expected compiler is
`x86_64-elf-gcc`.

```bash
xmake
```

Run in Bochs:

```bash
xmake run kernel
```

The top-level `Makefile` also provides a Bochs run shortcut:

```bash
make run
```

Notes:

- Install or expose `x86_64-elf-gcc`, `x86_64-elf-g++`, `x86_64-elf-ld`, and
  related binutils before building.
- Install Bochs before running the kernel in an emulator.
- `test/bochsrc.bxrc` may contain host-specific paths. Update the BIOS, VGA BIOS,
  and disk-image paths for your local machine.
- Some Python helper commands in `bigos.py` are placeholders and should not be
  treated as complete automation.

## Architecture Overview

```text
               +---------------------+
               |      boot code      |
               | MBR/DBR/boot.s/.cc  |
               +----------+----------+
                          |
                          v
               +---------------------+
               |     kernel entry    |
               |     kernel.cc       |
               +----------+----------+
                          |
          +---------------+----------------+
          |               |                |
          v               v                v
   +-------------+  +-------------+  +-------------+
   | memory (mm) |  | interrupts  |  |  drivers    |
   | buddy/slab  |  | IDT/ISR/PIC |  | VGA/i8259   |
   +------+------+  +------+------+  +------+------+
          |                |                |
          +----------------+----------------+
                          |
                          v
               +---------------------+
               | kernel C++ support  |
               | KTL/new/delete/ABI  |
               +---------------------+
```

## Major Subsystems

### Boot

The bootloader is specific to x86/x86_64 and assumes a disk image layout that can
provide an exFAT partition containing a file named `kernel`.

- `src/arch/x86/boot/mbr.s`: first-stage boot code.
- `src/arch/x86/boot/dbr_exfat.s`: exFAT boot-sector code.
- `src/arch/x86/boot/exdbr_exfat.s`: extended exFAT boot code.
- `src/arch/x86/boot/boot.s`: mode switching, early page tables, and transfer to long mode.
- `src/arch/x86/boot/boot.cc`: ATA disk reads, exFAT directory scan, and ELF64 load.
- `tools/install.py`: helper for writing boot sectors into a virtual disk image.

### Kernel Entry

`src/kernel/kernel.cc` currently performs the minimal runtime setup:

- Clears the VGA text screen.
- Calls `bigos::init_mem()`.
- Calls `bigos::irq::initIRQ()`.
- Enables interrupts.
- Idles with `hlt`.

### Memory Management

The memory subsystem lives under `src/mm/`.

- `src/mm/buddy.cc`: parses the BIOS memory map, separates DMA/DMA32/NORMAL zones, and
  manages physical page blocks.
- `src/mm/slab.cc` and `src/mm/kmem.cc`: provide fixed-size caches and `kmalloc/free`.
- `src/mm/vmem.cc`: tracks kernel virtual address blocks and can pre-map allocated pages.
- `include/bigos/memory.h`: exposes the public allocation API.
- `src/mm/memdef.h`: defines mm-private page size, buddy order, and allocation flags.

### Interrupts And Input

The interrupt subsystem combines assembly stubs and C++ descriptors.

- `src/kernel/irq/interrupt.s`: generated ISR entry stubs.
- `src/kernel/irq/interrupt.cc`: IDT initialization and default IRQ handler.
- `src/drivers/irqchip/i8259.cc`: PIC masking and EOI support.
- `src/kernel/irq/isr.cc`: keyboard scan-code parsing and temporary VGA
  character output.

Keyboard input is not yet routed through a complete TTY layer.

### Display And IO

VGA text mode is the current display backend.

- `src/drivers/video/vga.cc`: text buffer writes, cursor movement, screen clearing.
- `src/kernel/bigos/io.cc`: port IO wrappers and basic kernel printing.

### Kernel C++ Support

The project provides a small amount of freestanding C++ infrastructure.

- `cpp/include/ktl`: kernel container and utility headers.
- `cpp/ktl`: KTL implementations.
- `cpp/libsupc++`: minimal ABI and `new`/`delete` support.
- `include`: lightweight standard-style headers such as `stdint.h`, `stddef.h`,
  `stdarg.h`, and `string.h`.

## Development Notes

- Keep code freestanding-safe. Do not rely on hosted libc, exceptions, RTTI, OS
  services, or dynamic allocation paths that are not initialized yet.
- Treat boot addresses, linker addresses, page-table layout, interrupt vectors,
  and disk offsets as design-critical.
- Prefer small, explicit hardware-facing code.
- Validate initialization order carefully; many subsystems depend on memory,
  paging, or descriptor tables being available first.
- When touching Bochs or disk-image setup, document local path assumptions.

## License

BigOS is licensed under the GNU General Public License v3.0 only. See
`LICENSE`.
