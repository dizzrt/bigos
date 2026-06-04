# BigOS

Language: English | [简体中文](README-zh.md)

BigOS is an early-stage x86_64 operating system kernel written mainly in
freestanding C++17, C17, and assembly. It currently focuses on bootstrapping,
text/serial output, interrupt and exception handling, a minimal keyboard IRQ
smoke path, and a fairly complete early kernel memory-management stack.

This repository is a research/toy OS kernel project, not a hosted application or
service.

## Status

The project is in kernel infrastructure bring-up stage, with the boot path,
interrupt foundation, and early memory management now reasonably complete.

Implemented or partially implemented:

- x86 boot path with MBR, exFAT DBR, extended DBR, and long-mode transition.
- ELF64 kernel loading from an exFAT disk image.
- Higher-half kernel linking at `0xffffffff80000000`.
- VGA text-mode output, `kprintf`, and COM1 serial output for deterministic markers.
- Kernel-owned static IDT, generated assembly ISR stubs, and a stable
  `InterruptFrame` dispatch ABI that separates CPU exceptions from i8259 IRQs.
- Diagnostic-only `#PF` handler that reads `CR2` and emits a `BIGOS_PAGE_FAULT` marker.
- i8259 PIC driver and a minimal keyboard IRQ1 scan-code smoke path.
- Buddy physical page allocator with an early metadata arena for bootstrap.
- Slab/kmalloc allocator with size classes, dynamic slab reclaim, page-backed
  large allocations, optional debug guards, and validation statistics.
- Kernel virtual-memory allocator (first-fit, 4-level page-table mapping, PTE
  clearing and TLB invalidation on free) plus C++ `new`/`delete` integration.
- Explicit allocation API: `alloc_kernel_pages(nr_pages, flags)` for kernel
  virtual pages and an internal `alloc_physical_order(order, flags)` for buddy.
- Switchable early memory runtime self-test (`bigos::mm::self_test`).
- Small KTL support library for kernel containers and helpers.

Not implemented or still skeletal:

- UEFI bootloader, ESP image generation, and OVMF/QEMU UEFI smoke tests.
- TTY, full keyboard input, and console abstraction beyond VGA/serial output.
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
  - initializes memory management (init_mem)
  - optionally runs the early memory runtime self-test (mm::self_test)
  - initializes kernel-owned IDT, exception dispatch, i8259 PIC, and keyboard IRQ1 smoke
  - enables interrupts after selected early handlers are registered
  - emits the "BigOS kernel reached" marker on serial and VGA
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

Optional validation build switches (off by default):

```bash
xmake f --mm_self_test=y    # run the early memory runtime self-test on boot
xmake f --slab_debug=y      # enable slab allocator debug guards
xmake f --page_fault_smoke=y # enable the validation-only page-fault trigger
```

`--mm_self_test` implies `--slab_debug`. The self-test emits the
`BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` markers on COM1 and VGA.

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
python3 tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
python3 tools/boot_debug.py validate-image --image build/test/os.raw
```

`--memory-self-test` builds with `BIGOS_MM_SELF_TEST` and routes COM1 to a serial
log; combine with `--expect-serial-marker`/`--smoke-timeout` for a bounded smoke
test. Run Python helpers through `uv run` per the project tooling convention,
e.g. `uv run python tools/boot_debug.py run --no-launch`.

A GUI shortcut is also available:

```bash
make boot-debug-gui
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

`src/kernel/kernel.cc` performs the current runtime setup:

- Clears the VGA text screen.
- Calls `bigos::init_mem()`.
- Optionally runs `bigos::mm::self_test()` (when built with `BIGOS_MM_SELF_TEST`).
- Calls `bigos::irq::initIRQ()`.
- Optionally triggers a page-fault smoke (when built with `BIGOS_PAGE_FAULT_SMOKE`).
- Enables interrupts and emits the "BigOS kernel reached" marker.
- Idles with `hlt`.

### Memory Management

The memory subsystem lives under `src/mm/` and is the most developed part of the
kernel. The public API distinguishes allocation layers by name: kernel virtual
pages use page-count semantics through `alloc_kernel_pages(nr_pages, flags)`,
while internal physical allocation uses buddy-order semantics through
`alloc_physical_order(order, flags)`. Legacy mixed-semantics aliases were removed.

- `src/mm/buddy.cc` / `src/mm/buddy.h`: parse the BIOS memory map, separate
  DMA/DMA32/NORMAL zones, manage physical page blocks, and use an early metadata
  arena for bootstrap so initialization does not depend on dynamic slab growth.
- `src/mm/slab.cc` / `src/mm/slab.h`: size-class caches, `kmalloc/free`, dynamic
  slab reclaim, page-backed large allocations, optional debug guards, and stats.
- `src/mm/kmem.cc` / `src/mm/kmem.h`: kmalloc/free integration and cache wiring.
- `src/mm/vmem.cc` / `src/mm/vmem.h`: first-fit kernel virtual ranges, 4-level
  page-table mapping, and PTE clear plus TLB invalidation on free.
- `src/mm/memory.cc`: public memory API entry points.
- `src/mm/self_test.cc`: switchable early runtime self-test with deterministic
  `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` markers.
- `include/bigos/memory.h`: exposes the public allocation API.
- `src/mm/memdef.h`: defines mm-private page size, buddy order, and allocation flags.

See `docs/arch/memory-runtime-validation.md` for self-test usage.

### Interrupts And Input

The interrupt subsystem combines assembly stubs and C++ descriptors. The kernel
loads a kernel-owned static IDT before enabling maskable interrupts and routes
all ISR entries through a stable `InterruptFrame` dispatch ABI.

- `src/kernel/irq/interrupt.s`: generated ISR entry stubs.
- `src/kernel/irq/interrupt.cc`: IDT initialization, exception dispatch, and external IRQ dispatch.
- `src/drivers/irqchip/i8259.cc`: PIC remap, masking, and EOI support.
- `src/kernel/irq/isr.cc`: diagnostic `#PF` handler and minimal keyboard IRQ1 scancode smoke.
- `include/irq/interrupt.h`: descriptor layout, `InterruptFrame`, and vector constants.
- `docs/arch/interrupt-exception-foundation.md`: current interrupt/exception
  design, non-goals, and validation notes.

CPU exceptions (`0x00`-`0x1f`) and remapped i8259 IRQs (`0x20`-`0x2f`) are
dispatched separately; EOI is only sent for external IRQs. Keyboard input is not
yet routed through a complete TTY layer; the current IRQ1 path only reads one
PS/2 scancode byte and prints a smoke marker.

### Display And IO

VGA text mode and COM1 serial are the current output backends.

- `src/drivers/video/vga.cc`: text buffer writes, cursor movement, screen clearing.
- `src/kernel/bigos/io.cc`: port IO wrappers, `kprintf`, and serial output.
- `src/kernel/bigos/utils.cc`: small helpers such as integer-to-string conversion.

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
