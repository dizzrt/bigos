# BigOS

Language: English | [简体中文](README-zh.md)

BigOS is an early-stage x86_64 operating system kernel written mainly in
freestanding C++17, C17, and assembly. It has grown from a boot/kernel skeleton
into a single-core kernel with a minimal user-mode loop: bootstrapping,
text/serial output, interrupt/exception/syscall handling, a PIT timer tick, a
keyboard-driven TTY input path, a cooperative kernel-thread scheduler, an
`int 0x80` syscall entry, default-off ring3 user-program smokes, a bounded
ELF64 user-program loader, a read-only block/exFAT path, and a fairly complete
early kernel memory-management stack.

This repository is a research/toy OS kernel project, not a hosted application or
service.

## Status

The project has iterated past kernel infrastructure bring-up into a single-core
kernel with timer, input, scheduling, syscall, read-only block/exFAT services,
bounded user ELF loading, and minimal user-mode smokes on top of the boot path,
interrupt foundation, and early memory management.

Implemented or partially implemented:

- x86 boot path with MBR, exFAT DBR, extended DBR, and long-mode transition.
- ELF64 kernel loading from an exFAT disk image.
- Higher-half kernel linking at `0xffffffff80000000`.
- VGA text-mode output, `kprintf`, and COM1 serial output for deterministic markers.
- Kernel-owned static IDT, generated assembly ISR stubs, and a stable
  `InterruptFrame` dispatch ABI that separates CPU exceptions, i8259 IRQs, and
  the `int 0x80` syscall vector.
- Diagnostic-only `#PF` handler that reads `CR2` and emits a `BIGOS_PAGE_FAULT` marker.
- Unified early fatal diagnostics (`bigos::kpanic`/`khalt`) that emit a stable
  `BIGOS_PANIC code=<code> source=<source>` marker on COM1 and VGA, then disable
  interrupts and halt.
- i8259 PIC driver and a PIT timer driver on IRQ0 providing a monotonic tick and
  a minimal `mdelay` busy-wait primitive.
- Keyboard IRQ1 scancode decode (US Set 1) feeding a fixed-capacity TTY/console
  input path; console output routes through VGA.
- Cooperative (non-preemptive) single-core kernel-thread scheduler with 1-page
  kernel stacks, an x86_64 context switch, a round-robin `yield()`, and an idle
  thread; the timer IRQ only records bounded reschedule intent.
- `int 0x80` syscall entry with a minimal register ABI and a diagnostic
  dispatcher (`SYS_DEBUG_WRITE`, `SYS_GET_TICK`, `SYS_WRITE`, `SYS_EXIT`).
- Default-off first ring3 user program smoke: a flat embedded image enters
  ring3 via TSS/RSP0 and `iretq`, then completes a `SYS_WRITE`/`SYS_EXIT` loop.
- Default-off user ELF smoke: a bounded ELF64 `ET_EXEC` image is packaged as
  `/boot/user/init.elf`, read from exFAT, mapped into a derived user address
  space, and entered in ring3.
- Read-only kernel block/filesystem path: synchronous ATA PIO sector reads,
  MBR exFAT partition discovery, read-only mount, absolute path lookup, and
  bounded file reads for controlled Bochs raw images.
- Buddy physical page allocator with an early metadata arena for bootstrap.
- Slab/kmalloc allocator with size classes, dynamic slab reclaim, page-backed
  large allocations, optional debug guards, and validation statistics.
- Kernel virtual-memory allocator (first-fit, 4-level page-table mapping, PTE
  clearing and TLB invalidation on free), a kernel direct map, plus C++
  `new`/`delete` integration.
- User address-space teardown and empty PT/PD/PDPT reclamation for owned
  runtime-created mappings, with high-half kernel mappings kept borrowed.
- Explicit allocation API: `alloc_kernel_pages(nr_pages, flags)` for kernel
  virtual pages and an internal `alloc_physical_order(order, flags)` for buddy.
- Switchable early memory runtime self-test (`bigos::mm::self_test`).
- Small KTL support library for kernel containers and helpers.

Not implemented or still skeletal:

- UEFI bootloader, ESP image generation, and OVMF/QEMU UEFI smoke tests.
- Preemptive scheduling, priorities, time slices, sleep queues, and blocking.
- A full multi-process model: multi-process scheduling, fork, general exec
  semantics with argv/envp and file descriptors, signals, and broad process
  lifecycle policy beyond the bounded smokes.
- Demand paging, copy-on-write, VMAs, `mmap`, `brk`, and user-space libc.
- Writable filesystems, a VFS, page cache, and broad storage-device support
  beyond the current synchronous read-only ATA PIO plus exFAT subset.
- Broad device-driver support.
- Complete build/install automation and CI.

## Repository Layout

```text
.
|-- cpp               kernel C++ support library, KTL, libsupc++ subset
|-- include           public kernel headers and small libc-style header subset
|-- src               implementation sources for boot, kernel, drivers, mm, runtime
|   |-- arch/x86/boot x86 boot code, MBR/DBR, and ELF loader
|   |-- drivers       hardware drivers such as VGA, i8259 PIC, PIT, and ATA PIO
|   |-- kernel        kernel entry and subsystems: irq, timer, terminal (console/
|   |                 keyboard/tty), sched, syscall, proc, fs, low-level IO
|   |-- mm            buddy, slab, kmalloc, virtual memory, and direct map code
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
  - initializes serial output (serial_init)
  - initializes memory management (init_mem)
  - optionally runs the early memory runtime self-test (mm::self_test) and the
    user-address-space vmem smoke
  - initializes the TTY/console input path (terminal::init_tty)
  - initializes kernel-owned IDT, exception/IRQ/syscall dispatch, i8259 PIC, the
    PIT timer on IRQ0, and the keyboard IRQ1 path
  - enables interrupts after early handlers are registered
  - emits the "BigOS kernel reached" marker on serial and VGA
  - optionally runs the syscall, scheduler, block/exFAT, first-user-program, and
    user-ELF smokes
  - enters the cooperative scheduler via sched::start(); halt behavior is owned
    by the scheduler idle thread instead of a naked hlt loop
```

Key files:

- `src/arch/x86/boot/boot.s`: early CPU mode switch and jump to the kernel.
- `src/arch/x86/boot/boot.cc`: disk read, exFAT lookup, ELF loading.
- `src/kernel/kernel.cc`: main kernel entry.
- `link.lds`: higher-half kernel layout.
- `docs/en/arch/x86-boot-layout.md`: current Legacy BIOS address and handoff layout.
- `docs/en/arch/uefi-boot-blueprint.md`: future UEFI compatibility blueprint; this is
  project planning only and is not a currently runnable boot path.

## Build And Run

The primary build system is xmake and the expected compiler is
`x86_64-elf-gcc`.

```bash
xmake
```

Optional validation build switches (all off by default; see `xmake.lua`):

```bash
xmake f --mm_self_test=y      # BIGOS_MM_SELF_TEST -> BIGOS_MM_SELF_TEST_PASSED/FAILED
xmake f --slab_debug=y        # BIGOS_SLAB_DEBUG slab debug guards (implied by mm_self_test)
xmake f --page_fault_smoke=y  # BIGOS_PAGE_FAULT_SMOKE -> BIGOS_PAGE_FAULT
xmake f --timer_smoke=y       # BIGOS_TIMER_SMOKE -> BIGOS_TIMER_IRQ
xmake f --keyboard_smoke=y    # BIGOS_KEYBOARD_SMOKE enables the IRQ1 line
xmake f --scheduler_smoke=y   # BIGOS_SCHEDULER_SMOKE -> BIGOS_SCHED_THREAD_A/B
xmake f --user_vmem_smoke=y   # BIGOS_USER_VMEM_SMOKE -> BIGOS_USER_VMEM_SMOKE_PASSED/FAILED
xmake f --syscall_smoke=y     # BIGOS_SYSCALL_SMOKE -> BIGOS_SYSCALL_SMOKE_PASSED/FAILED
xmake f --user_program_smoke=y # BIGOS_USER_PROGRAM_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --fs_smoke=y          # BIGOS_FS_SMOKE -> BIGOS_FS_EXFAT_READ_PASSED/FAILED
xmake f --user_elf_smoke=y    # BIGOS_USER_ELF_SMOKE -> BIGOS_USER_ENTER/EXIT
```

`--mm_self_test` implies `--slab_debug`. The self-test emits the
`BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` markers on COM1 and VGA.
`--user_program_smoke` and `--user_elf_smoke` additionally compile
`src/kernel/proc/**`; the ELF smoke also builds and packages
`build/bin/user/init.elf` as `/boot/user/init.elf`. These smokes are not part of
a normal boot. All markers are written to COM1 serial and VGA.

Local Bochs runs for the current Legacy BIOS/MBR/exFAT path:

```bash
xmake run bochs-sdl2
xmake run bochs
```

`xmake run bochs-sdl2` launches the SDL2 Bochs flow. `xmake run bochs` is the
non-SDL2 fallback. Both targets build the kernel and boot artifacts through
xmake, then call the Python image/Bochs helper with `--skip-build`.

The Python helper remains useful for raw-image generation, generated Bochs
configuration, serial-marker checks, and no-launch/offline validation:

```bash
uv run python tools/boot_debug.py run
```

The helper runs preflight checks, can build the kernel and boot artifacts when
not called from an xmake run target, creates a raw disk image entirely in user
space, writes the MBR, exFAT boot regions, `/boot/boot.bin`, root `kernel`,
`/boot/fs_smoke.txt`, and optional `/boot/user/init.elf`, then launches Bochs
unless `--no-launch` is supplied. It does not build a UEFI loader, ESP image, or
OVMF configuration.

Generated boot-debug artifacts are isolated under `build/` by default:

- Raw disk image: `build/test/os.raw`.
- Generated Bochs config: `build/test/bochsrc.bxrc`.
- Boot artifacts: `build/bin/x86/boot/`.
- Kernel ELF: `build/kernel`.

Useful options:

```bash
uv run python tools/boot_debug.py run --image build/test/debug.raw --image-size 128M
uv run python tools/boot_debug.py run --no-launch
uv run python tools/boot_debug.py run --romimage /path/to/BIOS-bochs-latest --vgaromimage /path/to/VGABIOS-lgpl-latest
uv run python tools/boot_debug.py run --serial-log build/test/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
uv run python tools/boot_debug.py validate-image --image build/test/os.raw
```

Use `xmake f ...=y` to configure smoke switches before invoking xmake run
targets. The Python helper is not the authoritative smoke-switch configuration
surface.

The raw image builder uses only Python standard library file writes. It does not
require macOS `diskutil`, Linux loop devices, mount permissions, `mkfs.exfat`, or
a hand-prepared exFAT image.

Current scope:

- Bochs is the only supported emulator in this workflow.
- `xmake run bochs-sdl2` is the SDL2 Legacy BIOS debug entry, and
  `xmake run bochs` is the non-SDL2 fallback. A future UEFI workflow is planned
  as a separate path with isolated ESP/FAT image artifacts and QEMU + OVMF as
  the preferred smoke-test path.
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

Run the generated Bochs flows:

```bash
xmake run bochs-sdl2
xmake run bochs
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
parallel backend in `docs/en/arch/uefi-boot-blueprint.md`.

- `src/arch/x86/boot/mbr.s`: first-stage boot code.
- `src/arch/x86/boot/dbr_exfat.s`: exFAT boot-sector code.
- `src/arch/x86/boot/exdbr_exfat.s`: extended exFAT boot code.
- `src/arch/x86/boot/boot.s`: mode switching, early page tables, and transfer to long mode.
- `src/arch/x86/boot/boot.cc`: ATA disk reads, exFAT directory scan, and ELF64 load.
- `tools/install.py`: helper for writing boot sectors into a virtual disk image.

### Kernel Entry

`src/kernel/kernel.cc` performs the current runtime setup:

- Clears the VGA text screen and initializes COM1 serial output.
- Calls `bigos::init_mem()`.
- Optionally runs `bigos::mm::self_test()` (when built with `BIGOS_MM_SELF_TEST`)
  and the user-address-space vmem smoke (`BIGOS_USER_VMEM_SMOKE`).
- Initializes the TTY/console input path (`bigos::terminal::init_tty()`).
- Calls `bigos::irq::initIRQ()` (IDT, exception/IRQ/syscall dispatch, PIC, PIT
  timer on IRQ0, keyboard IRQ1).
- Optionally triggers a page-fault smoke (when built with `BIGOS_PAGE_FAULT_SMOKE`).
- Enables interrupts and emits the "BigOS kernel reached" marker.
- Optionally runs the syscall, scheduler, and first-user-program smokes.
- Enters the cooperative scheduler via `bigos::sched::start()`; the idle thread
  owns the halt behavior.

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

See `docs/en/arch/memory-runtime-validation.md` for self-test usage.

### Interrupts And Input

The interrupt subsystem combines assembly stubs and C++ descriptors. The kernel
loads a kernel-owned static IDT before enabling maskable interrupts and routes
all ISR entries through a stable `InterruptFrame` dispatch ABI.

- `src/kernel/irq/interrupt.s`: generated ISR entry stubs.
- `src/kernel/irq/interrupt.cc`: IDT initialization, exception dispatch, external
  IRQ dispatch, and routing of the `int 0x80` syscall vector.
- `src/drivers/irqchip/i8259.cc`: PIC remap, masking, and EOI support.
- `src/kernel/irq/isr.cc`: diagnostic `#PF` handler, the timer IRQ0 tick hook,
  and the keyboard IRQ1 handler.
- `include/irq/interrupt.h`: descriptor layout, `InterruptFrame`, and vector constants.
- `docs/en/arch/interrupt-exception-foundation.md`: current interrupt/exception
  design, non-goals, and validation notes.

CPU exceptions (`0x00`-`0x1f`), remapped i8259 IRQs (`0x20`-`0x2f`), and the
syscall vector (`0x80`) are dispatched separately; EOI is only sent for external
IRQs. The syscall IDT gate is raised to DPL=3 so ring3 can issue `int 0x80`,
while exception and external IRQ gates remain ring0-only.

### Timer

The PIT drives a periodic IRQ0 tick.

- `src/drivers/timer/pit.cc` / `include/drivers/timer/pit.h`: PIT channel-0 setup.
- `src/kernel/timer/timer.cc` / `include/bigos/timer.h`: a monotonic `ticks()`
  counter advanced from IRQ context via `on_tick()`, and a minimal `mdelay`
  busy-wait. The `timer_smoke` switch emits a bounded `BIGOS_TIMER_IRQ` marker.

### TTY, Console, And Keyboard

Keyboard input flows from the IRQ1 handler through scancode decode into a
fixed-capacity TTY input buffer; console output routes through VGA.

- `src/kernel/terminal/keyboard.cc` / `include/bigos/keyboard.h`: US Set 1
  scancode decode with modifier tracking; the IRQ does only bounded decode and a
  ring-buffer handoff.
- `src/kernel/terminal/tty.cc` / `include/bigos/tty.h`: TTY input enqueue and
  `terminal::init_tty()`.
- `src/kernel/terminal/console.cc` / `include/bigos/console.h`: console output
  over the VGA backend.

### Scheduler

A cooperative, single-core kernel-thread scheduler.

- `src/kernel/sched/sched.cc` / `include/bigos/sched.h`: TCBs, a round-robin run
  queue, `create_kernel_thread()`, `yield()`, `thread_exit()`, and `start()`
  (which adopts the boot context as the idle thread).
- `src/kernel/sched/switch.s`: the x86_64 callee-saved context switch.
- The timer IRQ records bounded reschedule intent only; it never preempts on IRQ
  return. The `scheduler_smoke` switch runs two worker threads emitting
  `BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B`.

### System Calls

- `src/kernel/syscall/syscall.cc` / `include/bigos/syscall.h`: the `int 0x80`
  dispatcher and minimal register ABI (number in `rax`, args in
  `rdi/rsi/rdx/r10/r8/r9`, return in `rax`). Implements `SYS_DEBUG_WRITE`,
  `SYS_GET_TICK`, `SYS_WRITE`, and `SYS_EXIT`; unknown numbers return
  `SYS_ENOSYS`. The `syscall_smoke` switch exercises this from ring0.

### Process And User Mode

Compiled only under `user_program_smoke` or `user_elf_smoke`, and not part of a
normal boot.

- `src/kernel/proc/proc.cc` / `include/bigos/proc.h`: a minimal `Process`,
  user address-space derivation, safe teardown/reaping, mapping of a flat
  embedded user image, and a bounded ELF64 `ET_EXEC` loader for
  `/boot/user/init.elf`. Both smoke paths exercise the `SYS_WRITE`/`SYS_EXIT`
  closed loop (`BIGOS_USER_ENTER` / `BIGOS_USER_EXIT`).
- `src/kernel/proc/user_mode.cc` / `src/kernel/proc/user_mode.s` /
  `include/bigos/user_mode.h`: GDT/TSS/RSP0 setup and the `iretq` ring3 entry.
- Demand paging is not implemented; the `#PF` handler records a controlled
  marker for user faults and hands process cleanup to the safe reaper.

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
