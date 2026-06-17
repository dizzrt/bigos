# BigOS

Language: English | [简体中文](README-zh.md)

BigOS is an early-stage x86_64 operating system kernel written mainly in
freestanding C++17, C17, and assembly. It has grown from a boot/kernel skeleton
into a smoke-tested, single-core, mostly synchronous research kernel. The
current default runnable baseline remains the x86_64 Legacy BIOS/MBR/exFAT
path, with a runnable x86_64 UEFI boot backend spike available as a non-parity
backend. Its bounded userland loop includes bootstrapping, text/serial output,
interrupt/exception/syscall handling, a PIT timer tick, keyboard-driven TTY
input, a bounded timer-aware kernel-thread scheduler, wait queues and timeout
sleep, an `int 0x80` syscall entry, process lifecycle core, fd/VFS services,
bounded writable `/rw` storage, persistent clean-sync `/rw`, cwd/relative path
handling, constrained rename, metadata queries, pipes/dup, default-on PID-1
init, a minimal user crt0/libc, `/bin/sh`, bounded ELF64 user-program loading,
VMA-backed user-memory validation, and a fairly complete early kernel
memory-management stack.

This repository is a research/toy OS kernel project, not a hosted application or
service.

## Status

The completed capability baseline is compressed into the current bounded
minimal usable system baseline: a single-core, mostly synchronous kernel with
timer, input, scheduling, syscall, a bounded POSIX-like process/I/O subset,
read/write VFS primitives, bounded user ELF loading, a resident PID-1 init,
minimal static user programs, bounded `/rw` runtime and persistent clean-sync
storage, cwd/relative path handling, constrained rename, metadata, pipe/dup,
and a minimal userland runtime on top of the boot path, interrupt foundation,
and early memory management.

Implemented or partially implemented:

- x86 boot path with MBR, exFAT DBR, extended DBR, and long-mode transition.
- ELF64 kernel loading from an exFAT disk image.
- Higher-half kernel linking at `0xffffffff80000000`.
- VGA text-mode output, `kprintf`, and COM1 serial output for deterministic markers.
- Kernel-owned static IDT, generated assembly ISR stubs, and a stable
  `InterruptFrame` dispatch ABI that separates CPU exceptions, i8259 IRQs, and
  the `int 0x80` syscall vector.
- `#PF` handler that reads `CR2`, recovers supported user demand-zero/COW faults,
  and emits `BIGOS_PAGE_FAULT` for unrecoverable kernel faults.
- Unified early fatal diagnostics (`bigos::kpanic`/`khalt`) that emit a stable
  `BIGOS_PANIC code=<code> source=<source>` marker on COM1 and VGA, then disable
  interrupts and halt.
- i8259 PIC driver and a PIT timer driver on IRQ0 providing a monotonic tick and
  a minimal `mdelay` busy-wait primitive.
- Keyboard IRQ1 scancode decode (US Set 1) feeding a fixed-capacity TTY/console
  input path; console output routes through VGA.
- Single-core kernel-thread scheduler with 1-page kernel stacks, an x86_64
  context switch, round-robin `yield()`, scheduler-owned idle thread, wait
  queues, timeout sleep, preemption-disable guards, and bounded IRQ-return timer
  preemption.
- `int 0x80` syscall entry with a minimal register ABI and a bounded dispatcher
  including process, fd/VFS, pipe/dup, identity/time, signal, and `SYS_EXECVE`
  calls used by the user libc and shell.
- Default-off first ring3 user program smoke: a flat embedded image enters
  ring3 via TSS/RSP0 and `iretq`, then completes a `SYS_WRITE`/`SYS_EXIT` loop.
- Default-off user ELF smoke: a bounded ELF64 `ET_EXEC` image is packaged as
  `/boot/user/init.elf`, read from exFAT, mapped into a derived user address
  space, and entered in ring3.
- Default-on resident C init packaged as `/boot/user/init.elf`, which starts and
  restarts `/bin/sh` with `fork` + `execve`, and a default-off
  `userland_smoke` path that validates crt0, libc, fork/exec/wait, pipe,
  redirection, and malloc with `BIGOS_USERLAND_PASSED`.
- Process lifecycle core with PID/parent-child state, wait/exit/reap semantics,
  process-local fd table, bounded exec image replacement, and safe teardown on
  exit or user fault.
- Kernel block/filesystem path and VFS shell: synchronous ATA PIO sector reads,
  MBR exFAT partition discovery, read-only exFAT boot assets, bounded writable
  `/rw` files, persistent clean-sync storage, cwd/relative path lookup,
  constrained rename, metadata queries, fd-backed
  `open`/`read`/`write`/`close`/`lseek`/`fsync`, and bounded file reads/writes
  for controlled raw images.
- Buddy physical page allocator with an early metadata arena for bootstrap.
- Slab/kmalloc allocator with size classes, dynamic slab reclaim, page-backed
  large allocations, optional debug guards, and validation statistics.
- Kernel virtual-memory allocator (first-fit, 4-level page-table mapping, PTE
  clearing and TLB invalidation on free), a kernel direct map, plus C++
  `new`/`delete` integration.
- User address-space teardown and empty PT/PD/PDPT reclamation for owned
  runtime-created mappings, with high-half kernel mappings kept borrowed.
- Bounded VMA/user-memory API for stack/heap/image metadata, VMA-backed syscall
  buffer validation, `brk`, restricted anonymous mapping, demand-zero user
  materialization, and COW write splitting; this is not broad file-backed
  demand paging.
- Explicit allocation API: `alloc_kernel_pages(nr_pages, flags)` for kernel
  virtual pages and an internal `alloc_physical_order(order, flags)` for buddy.
- Switchable early memory runtime self-test (`bigos::mm::self_test`).
- Small KTL support library for kernel containers and helpers.

Not implemented or still skeletal:

- UEFI runtime parity, storage/device backend parity, backend cleanup, and broad
  UEFI validation beyond the current runnable x86_64 boot backend spike.
- SMP, per-CPU run queues, cross-CPU migration, full priority/realtime
  scheduling, and POSIX scheduling policy.
- A full POSIX multi-process model: broad process policy, `fork` semantics such
  as COW beyond the current bounded subset, `exec*` families, job control, and
  terminal process groups.
- File-backed `mmap`, broad mapping policy, dynamic linking, shared libraries,
  and a complete POSIX libc.
- Complete POSIX filesystems, journaling, crash recovery, async I/O, broad
  directory mutation semantics beyond the constrained current subset, and broad
  storage-device support beyond the current controlled ATA PIO plus exFAT/VFS
  subset.
- Broad device-driver support.
- Complete build/install automation and CI.

## Repository Layout

```text
.
|-- cpp               kernel C++ support library, KTL, libsupc++ subset
|-- include           public kernel headers and small libc-style header subset
|-- user              freestanding user crt0/libc, init, shell, bins, and smoke
|-- kernel            kernel implementation sources
|   |-- arch/x86/boot x86 boot code, MBR/DBR, and ELF loader
|   |-- core          kernel entry and subsystems: irq, timer, terminal (console/
|   |                 keyboard/tty), sched, syscall, proc, fs, low-level IO
|   |-- drivers       hardware drivers such as VGA, i8259 PIC, PIT, and ATA PIO
|   |-- mm            buddy, slab, kmalloc, virtual memory, and direct map code
|   `-- runtime       runtime startup assembly source objects
|-- tools             developer helpers such as the boot disk install tool
|-- openspec          OpenSpec project configuration
|-- tests             validation tests and local test assets
|-- link.lds          kernel linker script
|-- xmake             split xmake includes and cross-toolchain definition
|-- xmake.lua         primary build entry
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
  - optionally runs the syscall, scheduler, blocking, scheduler-semantics,
    block/exFAT, first-user-program, and user-ELF smokes
  - enters the single-core scheduler via sched::start(); halt behavior is owned
    by the scheduler idle thread instead of a naked hlt loop
```

Key files:

- `kernel/arch/x86/boot/boot.s`: early CPU mode switch and jump to the kernel.
- `kernel/arch/x86/boot/boot.cc`: disk read, exFAT lookup, ELF loading.
- `kernel/core/kernel.cc`: main kernel entry.
- `link.lds`: higher-half kernel layout.
- `docs/en/arch/x86-boot-layout.md`: current Legacy BIOS address and handoff layout.
- `docs/en/arch/uefi-boot-blueprint.md`: x86_64 UEFI boot backend spike notes
  and future runtime-parity/backend-cleanup boundaries.

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
xmake f --scheduler_semantics_smoke=y # BIGOS_SCHEDULER_SEMANTICS_SMOKE -> BIGOS_SCHED_SEMANTICS_PASSED
xmake f --blocking_smoke=y    # BIGOS_BLOCKING_SMOKE -> BIGOS_BLOCKING_SMOKE_PASSED
xmake f --user_vmem_smoke=y   # BIGOS_USER_VMEM_SMOKE -> BIGOS_USER_VMEM_SMOKE_PASSED/FAILED
xmake f --syscall_smoke=y     # BIGOS_SYSCALL_SMOKE -> BIGOS_SYSCALL_SMOKE_PASSED/FAILED
xmake f --user_program_smoke=y # BIGOS_USER_PROGRAM_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --fs_smoke=y          # BIGOS_FS_SMOKE -> BIGOS_FS_EXFAT_READ_PASSED/FAILED
xmake f --user_elf_smoke=y    # BIGOS_USER_ELF_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --demand_paging_smoke=y # BIGOS_DEMAND_PAGING_SMOKE -> BIGOS_DEMAND_PAGING_PASSED/FAILED
xmake f --fork_cow_smoke=y    # BIGOS_FORK_COW_SMOKE -> BIGOS_FORK_COW_PASSED/FAILED
xmake f --time_identity_smoke=y # BIGOS_TIME_IDENTITY_SMOKE -> BIGOS_TIME_IDENTITY_PASSED/FAILED
xmake f --signal_smoke=y      # BIGOS_SIGNAL_SMOKE -> BIGOS_SIGNAL_PASSED/FAILED
xmake f --writable_fs_smoke=y # BIGOS_WRITABLE_FS_SMOKE -> BIGOS_WRITABLE_FS_PASSED/FAILED
xmake f --pipe_smoke=y        # BIGOS_PIPE_SMOKE -> BIGOS_PIPE_PASSED/FAILED
xmake f --userland_smoke=y    # BIGOS_USERLAND_SMOKE -> BIGOS_USERLAND_PASSED/FAILED
```

`--mm_self_test` implies `--slab_debug`. The self-test emits the
`BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` markers on COM1 and VGA.
Normal boot packages `/boot/user/init.elf`, `/bin/sh`, and `/bin/*` helpers,
enters PID-1 init, and starts `/bin/sh`; default headless validation observes
`BIGOS_USER_EXEC`. `--user_program_smoke`, `--user_elf_smoke`, and
`--userland_smoke` select default-off user-program validation paths in place of
the normal user init payload. All markers are written to COM1 serial and VGA.

Local emulator runs for the current Legacy BIOS/MBR/exFAT path:

```bash
xmake run qemu
xmake run qemu -- --display none
xmake run qemu-gdb
xmake run bochs
xmake run bochs -- --display sdl2
xmake run bochs -- --display none
```

`xmake run qemu` launches graphical QEMU against the generated raw image and
writes COM1 output to `build/test/qemu.serial.log`. `xmake run qemu -- --display
none` launches QEMU without an interactive display. `xmake run qemu-gdb` starts
QEMU paused with the default GDB stub (`target remote localhost:1234`) and writes
COM1 output to `build/test/qemu-gdb.serial.log`. `xmake run bochs` defaults to
the SDL2 Bochs flow, `xmake run bochs -- --display sdl2` selects it explicitly,
and `xmake run bochs -- --display none` selects Bochs no-GUI mode. These targets
build the kernel and boot artifacts through xmake, then call the Python image
helper with `--skip-build` and forward target arguments after `--`.

The Python helper remains useful for raw-image generation, generated Bochs
configuration, QEMU launch command inspection, serial-marker checks, and
no-launch/offline validation:

```bash
uv run python tools/boot_debug.py run
```

The helper runs preflight checks, can build the kernel and boot artifacts when
not called from an xmake run target, creates a raw disk image entirely in user
space, writes the MBR, exFAT boot regions, `/boot/boot.bin`, root `kernel`,
`/boot/fs_smoke.txt`, `/boot/user/init.elf`, and packaged `/bin/*` user
programs, then launches the selected emulator unless `--no-launch` is supplied.
It does not build a UEFI loader, ESP image, OVMF configuration, or new
storage-driver path.

Generated boot-debug artifacts are isolated under `build/` by default:

- Raw disk image: `build/test/os.raw`.
- Generated Bochs config: `build/test/bochsrc.bxrc`.
- QEMU serial logs: `build/test/qemu.serial.log` and
  `build/test/qemu-gdb.serial.log`.
- Boot artifacts: `build/bin/x86/boot/`.
- Kernel ELF: `build/kernel`.

Useful options:

```bash
uv run python tools/boot_debug.py run --image build/test/debug.raw --image-size 128M
uv run python tools/boot_debug.py run --no-launch
uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
uv run python tools/boot_debug.py run --emulator qemu-gdb
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

- QEMU and Bochs are supported emulators in this workflow.
- QEMU uses Legacy BIOS defaults and exposes the raw image as an IDE disk with
  `-drive file=<image>,format=raw,if=ide`, preserving the current ATA PIO path.
- QEMU headless automation uses the helper display selector, for example
  `--emulator qemu --display none`; no separate `qemu-headless` xmake target is
  provided.
- `xmake run bochs` is the SDL2-by-default Legacy BIOS debug entry; use
  `xmake run bochs -- --display sdl2` for explicit SDL2 or `xmake run bochs --
  --display none` for Bochs no-GUI mode. Bochs remains useful for early boot,
  BIOS, ATA PIO, interrupt, and hardware-behavior investigations.
- UEFI remains a separate, non-parity backend spike path; storage/device parity
  and broader runtime validation remain future backend work.
- The workflow does not change `boot.s`, `boot.cc`, `BootInfo`, `link.lds`, the
  higher-half kernel address, or kernel runtime initialization.

Common failures:

- Missing `xmake`, selected emulator (`qemu-system-x86_64` or `bochs`),
  `python3`, or `x86_64-elf-*` tools are reported in the preflight stage before
  image generation or launch as appropriate.
- Kernel or boot build failures stop the workflow instead of continuing with
  stale artifacts.
- Bochs installations that require host-specific BIOS or VGA BIOS paths may need
  `--romimage`, `--vgaromimage`, `--bochsrc`, or `--bochs-extra`.
- Historical `test/bochsrc.bxrc` files are host-specific references only; the
  generated `build/test/bochsrc.bxrc` avoids Windows paths, `win32` display
  settings, and fixed ROM paths by default.

Run the generated Bochs flows:

```bash
xmake run qemu
xmake run qemu -- --display none
xmake run qemu-gdb
xmake run bochs
xmake run bochs -- --display sdl2
xmake run bochs -- --display none
```

Notes:

- Install or expose `x86_64-elf-gcc`, `x86_64-elf-g++`, `x86_64-elf-ld`, and
  related binutils before building.
- Install QEMU before using `xmake run qemu` or `xmake run qemu-gdb`; install
  Bochs before using the Bochs targets.
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
The default runnable backend is Legacy BIOS. A runnable x86_64 UEFI boot backend
spike exists, but it is not a runtime-parity replacement for the Legacy
BIOS/MBR/exFAT path.

- `kernel/arch/x86/boot/mbr.s`: first-stage boot code.
- `kernel/arch/x86/boot/dbr_exfat.s`: exFAT boot-sector code.
- `kernel/arch/x86/boot/exdbr_exfat.s`: extended exFAT boot code.
- `kernel/arch/x86/boot/boot.s`: mode switching, early page tables, and transfer to long mode.
- `kernel/arch/x86/boot/boot.cc`: ATA disk reads, exFAT directory scan, and ELF64 load.
- `tools/install.py`: helper for writing boot sectors into a virtual disk image.

### Kernel Entry

`kernel/core/kernel.cc` performs the current runtime setup:

- Clears the VGA text screen and initializes COM1 serial output.
- Calls `bigos::init_mem()`.
- Optionally runs `bigos::mm::self_test()` (when built with `BIGOS_MM_SELF_TEST`)
  and the user-address-space vmem smoke (`BIGOS_USER_VMEM_SMOKE`).
- Initializes the TTY/console input path (`bigos::terminal::init_tty()`).
- Calls `bigos::irq::initIRQ()` (IDT, exception/IRQ/syscall dispatch, PIC, PIT
  timer on IRQ0, keyboard IRQ1).
- Optionally triggers a page-fault smoke (when built with `BIGOS_PAGE_FAULT_SMOKE`).
- Enables interrupts and emits the "BigOS kernel reached" marker.
- Optionally runs the syscall, scheduler, blocking, scheduler-semantics,
  block/exFAT, first-user-program, and user-ELF smokes.
- Enters the single-core scheduler via `bigos::sched::start()`; the idle thread
  owns the halt behavior.

### Memory Management

The memory subsystem lives under `kernel/mm/` and is the most developed part of the
kernel. The public API distinguishes allocation layers by name: kernel virtual
pages use page-count semantics through `alloc_kernel_pages(nr_pages, flags)`,
while internal physical allocation uses buddy-order semantics through
`alloc_physical_order(order, flags)`. Legacy mixed-semantics aliases were removed.

- `kernel/mm/buddy.cc` / `kernel/mm/buddy.h`: parse the BIOS memory map, separate
  DMA/DMA32/NORMAL zones, manage physical page blocks, and use an early metadata
  arena for bootstrap so initialization does not depend on dynamic slab growth.
- `kernel/mm/slab.cc` / `kernel/mm/slab.h`: size-class caches, `kmalloc/free`, dynamic
  slab reclaim, page-backed large allocations, optional debug guards, and stats.
- `kernel/mm/kmem.cc` / `kernel/mm/kmem.h`: kmalloc/free integration and cache wiring.
- `kernel/mm/vmem.cc` / `kernel/mm/vmem.h`: first-fit kernel virtual ranges, 4-level
  page-table mapping, and PTE clear plus TLB invalidation on free.
- `kernel/mm/memory.cc`: public memory API entry points.
- `kernel/mm/self_test.cc`: switchable early runtime self-test with deterministic
  `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` markers.
- `include/bigos/memory.h`: exposes the public allocation API.
- `kernel/mm/memdef.h`: defines mm-private page size, buddy order, and allocation flags.

See `docs/en/arch/memory-runtime-validation.md` for self-test usage.

### Interrupts And Input

The interrupt subsystem combines assembly stubs and C++ descriptors. The kernel
loads a kernel-owned static IDT before enabling maskable interrupts and routes
all ISR entries through a stable `InterruptFrame` dispatch ABI.

- `kernel/core/irq/interrupt.s`: generated ISR entry stubs.
- `kernel/core/irq/interrupt.cc`: IDT initialization, exception dispatch, external
  IRQ dispatch, and routing of the `int 0x80` syscall vector.
- `kernel/drivers/irqchip/i8259.cc`: PIC remap, masking, and EOI support.
- `kernel/core/irq/isr.cc`: diagnostic `#PF` handler, the timer IRQ0 tick hook,
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

- `kernel/drivers/timer/pit.cc` / `include/drivers/timer/pit.h`: PIT channel-0 setup.
- `kernel/core/timer/timer.cc` / `include/bigos/timer.h`: a monotonic `ticks()`
  counter advanced from IRQ context via `on_tick()`, and a minimal `mdelay`
  busy-wait. The `timer_smoke` switch emits a bounded `BIGOS_TIMER_IRQ` marker.

### TTY, Console, And Keyboard

Keyboard input flows from the IRQ1 handler through scancode decode into a
fixed-capacity TTY input buffer; console output routes through VGA.

- `kernel/core/terminal/keyboard.cc` / `include/bigos/keyboard.h`: US Set 1
  scancode decode with modifier tracking; the IRQ does only bounded decode and a
  ring-buffer handoff.
- `kernel/core/terminal/tty.cc` / `include/bigos/tty.h`: TTY input enqueue and
  `terminal::init_tty()`.
- `kernel/core/terminal/console.cc` / `include/bigos/console.h`: console output
  over the VGA backend.

### Scheduler

A single-core round-robin kernel-thread scheduler with cooperative switch paths,
wait queues, timeout sleep, and guarded IRQ-return timer preemption.

- `kernel/core/sched/sched.cc` / `include/bigos/sched.h`: TCBs, a round-robin run
  queue, intrusive wait/sleep lists, `create_kernel_thread()`, `yield()`,
  `thread_exit()`, `sleep_for()`, wakeup APIs, preemption guards, and `start()`
  (which adopts the boot context as the idle thread).
- `kernel/core/sched/switch.s`: the x86_64 callee-saved context switch.
- The timer IRQ accounts ordinary-thread time slices, wakes expired sleepers,
  and records bounded reschedule intent. External IRQ return may switch only
  after handler completion, a single EOI, and scheduler guard approval.
- `scheduler_smoke`, `blocking_smoke`, and `scheduler_semantics_smoke` validate
  cooperative switching, wait/timeout behavior, and IRQ-return preemption markers.

### System Calls

- `kernel/core/syscall/syscall.cc` / `include/bigos/syscall.h`: the `int 0x80`
  dispatcher and minimal register ABI (number in `rax`, args in
  `rdi/rsi/rdx/r10/r8/r9`, return in `rax`). Implements `SYS_DEBUG_WRITE`,
  `SYS_GET_TICK`, `SYS_WRITE`, `SYS_EXIT`, `SYS_WAIT`, fd/VFS
  `SYS_OPEN`/`SYS_READ`/`SYS_CLOSE`, `SYS_BRK`, restricted `SYS_MAP_ANON`,
  `SYS_FORK`, time/identity, signal, `lseek`, pipe/dup, fsync/mkdir/unlink, and
  `SYS_EXECVE`; unknown numbers return `SYS_ENOSYS`. The `syscall_smoke` switch
  exercises diagnostic dispatch from ring0.

### Process And User Mode

The lifecycle core is compiled as a normal kernel subsystem. Normal boot enters
resident PID-1 init and `/bin/sh`; `user_program_smoke`, `user_elf_smoke`, and
`userland_smoke` only select default-off validation payloads.

- `kernel/core/proc/proc.cc` / `include/bigos/proc.h`: a bounded process model,
  PID allocation, parent/child linkage, wait status, zombie/reap-pending
  lifecycle, process-local fd table, VMA/heap metadata, VMA-backed user-buffer
  validation, `brk`, restricted anonymous mapping, demand-zero materialization,
  bounded `fork`/COW, signal state, user address-space derivation, safe
  teardown/reaping, mapping of a flat embedded user image, and bounded ELF64
  `ET_EXEC` prepare/exec paths for `/boot/user/init.elf`, `execve`, PID-1 init,
  and default-off user smokes.
- `kernel/core/proc/user_mode.cc` / `kernel/core/proc/user_mode.s` /
  `include/bigos/user_mode.h`: GDT/TSS/RSP0 setup and the `iretq` ring3 entry.
- Demand paging and COW are implemented only for the current bounded anonymous
  user mappings; broad file-backed `mmap`, dynamic linking, job control, and a
  complete POSIX libc remain out of scope.

### Display And IO

VGA text mode and COM1 serial are the current output backends.

- `kernel/drivers/video/vga.cc`: text buffer writes, cursor movement, screen clearing.
- `kernel/core/bigos/io.cc`: port IO wrappers, `kprintf`, and serial output.
- `kernel/core/bigos/utils.cc`: small helpers such as integer-to-string conversion.

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
- Keep fd/VFS, process lifecycle, and VMA/user-memory descriptions bounded:
  synchronous I/O, RAM-backed and persistent clean-sync `/rw`, constrained
  rename/metadata/cwd-relative paths, restricted anonymous/file mapping, bounded
  demand paging/COW, no broad POSIX process model, no dynamic linking, no job
  control, no journaling/crash recovery, and no broad file-backed `mmap`.
- Prefer small, explicit hardware-facing code.
- Validate initialization order carefully; many subsystems depend on memory,
  paging, or descriptor tables being available first.
- When touching Bochs or disk-image setup, document local path assumptions.

## License

BigOS is licensed under the GNU General Public License v3.0 only. See
`LICENSE`.
