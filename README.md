# BigOS

Language: English | [简体中文](README-zh.md)

BigOS is an early-stage x86_64 operating system kernel written mainly in
freestanding C++17, C17, and assembly. It currently focuses on bootstrapping,
- basic text output, interrupt/exception setup, minimal keyboard IRQ smoke, and early kernel
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
- Kernel-owned IDT setup, assembly interrupt stubs, and diagnostic CPU exception dispatch.
- i8259 PIC driver and minimal keyboard IRQ1 scan-code smoke.
- Buddy-based physical page allocation.
- Slab/kmalloc allocator and C++ `new`/`delete` integration.
- Early virtual-memory allocation and page-table mapping framework.
- Small KTL support library for kernel containers and helpers.

Not implemented or still skeletal:

- UEFI bootloader, ESP image generation, and OVMF/QEMU UEFI smoke tests.
- TTY, full keyboard input, and console abstraction beyond basic VGA output.
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
  - initializes kernel-owned IDT, exception dispatch, i8259 PIC, and keyboard IRQ1 smoke
  - enables interrupts after selected early handlers are registered
  - enters an idle hlt loop
```

Key files:

- `src/arch/x86/boot/boot.s`: early CPU mode switch and jump to the kernel.
- `src/arch/x86/boot/boot.cc`: disk read, exFAT lookup, ELF loading.
- `src/kernel/kernel.cc`: main kernel entry.
- `link.lds`: higher-half kernel layout.
- `docs/arch/x86-boot-layout.md`: current Legacy BIOS address and handoff layout.
- `docs/arch/uefi-boot-blueprint.md`: future UEFI compatibility blueprint; this is
  project planning only and is not a currently runnable boot path.

## Build And Run

The primary build system is xmake and the expected compiler is
`x86_64-elf-gcc`.

```bash
xmake
```

One-command local boot debug for the current Legacy BIOS/MBR/exFAT path:

```bash
make boot-debug
```

This is a thin wrapper around:

```bash
python3 tools/boot_debug.py run
```

The command runs preflight checks, builds the kernel with `xmake`, builds the boot
artifacts with `make -C src/arch/x86/boot build-mbr build-dbr build-exdbr
build-boot`, creates a raw disk image entirely in user space, writes the MBR,
exFAT boot regions, `/boot/boot.bin`, and root `kernel`, then launches Bochs.
It does not build a UEFI loader, ESP image, or OVMF configuration.

Generated boot-debug artifacts are isolated under `build/` by default:

- Raw disk image: `build/test/os.raw`.
- Generated Bochs config: `build/test/bochsrc.bxrc`.
- Boot artifacts: `build/bin/x86/boot/`.
- Kernel ELF: `build/kernel`.

Useful options:

```bash
python3 tools/boot_debug.py run --image build/test/debug.raw --image-size 128M
python3 tools/boot_debug.py run --no-launch
python3 tools/boot_debug.py run --romimage /path/to/BIOS-bochs-latest --vgaromimage /path/to/VGABIOS-lgpl-latest
python3 tools/boot_debug.py validate-image --image build/test/os.raw
```

The raw image builder uses only Python standard library file writes. It does not
require macOS `diskutil`, Linux loop devices, mount permissions, `mkfs.exfat`, or
a hand-prepared exFAT image.

First-stage scope:

- Bochs is the only supported emulator in this workflow.
- `make boot-debug` remains the Legacy BIOS debug entry. A future UEFI workflow is
  planned as a separate command such as `make uefi-boot-debug`, with isolated
  ESP/FAT image artifacts and QEMU + OVMF as the preferred smoke-test path.
- QEMU/headless mode, serial-log auto-detection, and CI smoke-test decisions are
  intentionally left for later changes.
- The workflow does not change `boot.s`, `boot.cc`, `BootInfo`, `link.lds`, the
  higher-half kernel address, or kernel runtime initialization.

Common failures:

- Missing `xmake`, `bochs`, `python3`, or `x86_64-elf-*` tools are reported in
  the preflight stage before image generation.
- Kernel or boot build failures stop the workflow instead of continuing with
  stale artifacts.
- Bochs installations that require host-specific BIOS or VGA BIOS paths may need
  `--romimage`, `--vgaromimage`, `--bochsrc`, or `--bochs-extra`.
- Historical `test/bochsrc.bxrc` files are host-specific references only; the
  generated `build/test/bochsrc.bxrc` avoids Windows paths, `win32` display
  settings, and fixed ROM paths by default.

Run an existing Bochs configuration:

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
The current runnable backend is Legacy BIOS; UEFI is documented as a future
parallel backend in `docs/arch/uefi-boot-blueprint.md`.

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
- `src/kernel/irq/interrupt.cc`: IDT initialization, exception dispatch, and external IRQ dispatch.
- `src/drivers/irqchip/i8259.cc`: PIC masking and EOI support.
- `src/kernel/irq/isr.cc`: minimal keyboard IRQ1 scancode smoke.
- `docs/arch/interrupt-exception-foundation.md`: current interrupt/exception
  design, non-goals, and validation notes.

Keyboard input is not yet routed through a complete TTY layer; the current IRQ1
path only reads one PS/2 scancode byte and prints a smoke marker.

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
