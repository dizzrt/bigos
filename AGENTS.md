# Agent Guide For BigOS

This repository contains BigOS, an early-stage x86_64 freestanding operating
system kernel. Treat it as low-level kernel code, not as a hosted application.

## Project Context

- BigOS is written primarily in C++17, C17, and x86/x86_64 assembly.
- xmake is the primary build system.
- `x86_64-elf-gcc` is the expected cross toolchain.
- QEMU and Bochs are supported local emulators for the Legacy BIOS/MBR/exFAT path.
- Python files are helper scripts unless explicitly stated otherwise.
- Run Python-related commands through `uv run` by default, including `pytest`,
  helper scripts, linting, formatting, and type checks.
- OpenSpec uses `openspec/config.yaml` for project context.

## Important Directories

- `src/arch/x86/boot`: boot sectors, long-mode transition, and ELF loader.
- `src/kernel`: kernel entry and subsystems:
  - `src/kernel/irq`: IDT, exception/IRQ/syscall dispatch, ISR stubs.
  - `src/kernel/timer`: monotonic tick and `mdelay`.
  - `src/kernel/terminal`: console output, keyboard scancode decode, TTY input.
  - `src/kernel/sched`: single-core scheduler, wait queues, timeout sleep,
    guarded IRQ-return preemption, and context-switch assembly.
  - `src/kernel/syscall`: `int 0x80` dispatcher and ABI.
  - `src/kernel/proc`: minimal process model, fd table, VMA/heap metadata,
    ring3 user-mode entry, safe teardown/reaping, and bounded ELF64
    user-program loading. Flat first-user-program and filesystem-backed user
    ELF entries remain default-off smoke consumers.
  - `src/kernel/fs`: read-only VFS shell over exFAT, path lookup, fd-backed
    open/read/close, and bounded file reads.
  - `src/kernel/bigos`: low-level IO, panic, and utility helpers.
- `src/mm`: buddy allocator, slab allocator, `kmalloc/free`, virtual memory, and
  the kernel direct map, including owned empty page-table reclamation.
- `src/drivers`: VGA text mode, i8259 PIC, PIT timer, and ATA PIO block drivers.
- `src/runtime`: startup assembly source objects.
- `tools`: developer helper scripts such as the boot disk install tool.
- `cpp`: kernel C++ support library, KTL containers, `new/delete`, ABI stubs.
- `include`: public kernel headers (`bigos/`, `drivers/`, `irq/`) and a small
  freestanding header subset.
- `tests`: validation tests and local test assets.
- `docs`: bilingual project documentation; `docs/en` is the canonical English
  tree and `docs/zh` is the Simplified Chinese mirror.

## Coding Rules

- Keep code freestanding-safe. Do not assume libc, a hosted runtime, exceptions,
  RTTI, threads, files, sockets, environment variables, or OS services.
- Preserve the current low-level style: explicit hardware constants, explicit
  address handling, and minimal hidden control flow.
- Use the existing namespaces:
  - `bigos` for kernel APIs.
  - `bigos::mm` for memory management.
  - `bigos::irq` for interrupt code.
  - `driver::*` for hardware drivers.
  - `ktl` for kernel containers and utilities.
- Use the explicit memory allocation API and do not reintroduce removed aliases:
  - `bigos::alloc_kernel_pages(nr_pages, flags)` for kernel virtual pages (page-count semantics).
  - internal `alloc_physical_order(order, flags)` for buddy physical pages (order semantics).
  - `bigos::kmalloc/free` for general kernel objects; `free_pages` for kernel virtual ranges.
  - Do not add `alloc_pages()`, `alloc_physical_pages()`, or `free_physical_pages()`.
- Keep public headers small. Include only what is required.
- Avoid adding dependencies unless the user explicitly asks and the dependency is
  valid for a freestanding kernel.
- Prefer ASCII for source and documentation unless a file already has a clear
  reason to use non-ASCII.
- For repository documentation, treat `docs/en` as canonical and keep `docs/zh`
  synchronized with matching relative Markdown paths. Refer to `docs/en` first;
  consult `docs/zh` when Simplified Chinese wording helps resolve ambiguity.
- Use repository-relative paths for documentation references. Do not commit local
  machine-specific absolute documentation paths.
- Do not silently change boot addresses, linker addresses, interrupt vectors,
  page-table self-mapping addresses, disk offsets, or ABI assumptions.

## Build And Run

Common commands:

```bash
xmake
xmake run qemu
xmake run qemu -- --display none
xmake run qemu-gdb
xmake run bochs
xmake run bochs -- --display sdl2
xmake run bochs -- --display none
```

Validation build switches (all off by default, see `xmake.lua`):

```bash
xmake f --mm_self_test=y       # BIGOS_MM_SELF_TEST: early memory runtime self-test
xmake f --slab_debug=y         # BIGOS_SLAB_DEBUG: slab debug guards (implied by mm_self_test)
xmake f --page_fault_smoke=y   # BIGOS_PAGE_FAULT_SMOKE -> BIGOS_PAGE_FAULT
xmake f --timer_smoke=y        # BIGOS_TIMER_SMOKE -> BIGOS_TIMER_IRQ
xmake f --keyboard_smoke=y     # BIGOS_KEYBOARD_SMOKE: enables the IRQ1 line
xmake f --scheduler_smoke=y    # BIGOS_SCHEDULER_SMOKE -> BIGOS_SCHED_THREAD_A/B
xmake f --scheduler_semantics_smoke=y # BIGOS_SCHEDULER_SEMANTICS_SMOKE -> BIGOS_SCHED_SEMANTICS_PASSED
xmake f --blocking_smoke=y     # BIGOS_BLOCKING_SMOKE -> BIGOS_BLOCKING_SMOKE_PASSED
xmake f --user_vmem_smoke=y    # BIGOS_USER_VMEM_SMOKE -> BIGOS_USER_VMEM_SMOKE_PASSED/FAILED
xmake f --syscall_smoke=y      # BIGOS_SYSCALL_SMOKE -> BIGOS_SYSCALL_SMOKE_PASSED/FAILED
xmake f --user_program_smoke=y # BIGOS_USER_PROGRAM_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --fs_smoke=y           # BIGOS_FS_SMOKE -> BIGOS_FS_EXFAT_READ_PASSED/FAILED
xmake f --user_elf_smoke=y     # BIGOS_USER_ELF_SMOKE -> BIGOS_USER_ENTER/EXIT
```

`--user_program_smoke` and `--user_elf_smoke` enter default-off bounded ring3
user-program paths; neither is part of a normal boot. `src/kernel/proc/**` is a
normal kernel subsystem, while the smoke switches control flat/user-ELF entry
threads and the optional user ELF artifact. `--user_elf_smoke` builds
`build/bin/user/init.elf`, packages it as `/boot/user/init.elf`, reads it through
the kernel VFS/exFAT path, and loads a bounded ELF64 `ET_EXEC` image.

For bounded emulator smoke against memory markers:

```bash
xmake f --mm_self_test=y
uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
```

The self-test emits `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED`,
the `#PF` handler emits `BIGOS_PAGE_FAULT`, and the unified panic path emits
`BIGOS_PANIC code=<code> source=<source>` on COM1 and VGA. Configure smoke
options with `xmake f ...`; `xmake run qemu` launches the graphical QEMU flow,
`xmake run qemu -- --display none` launches the QEMU headless flow, `xmake run
qemu-gdb` starts QEMU paused with the default GDB stub, and `xmake run bochs`
launches the SDL2 Bochs flow by default. Use `xmake run bochs -- --display none`
for Bochs no-GUI mode.

Notes:

- Verify that `x86_64-elf-gcc`, `x86_64-elf-g++`, `x86_64-elf-ld`, and related
  binutils are installed before building.
- Generated emulator outputs are written under `build/test/`. QEMU uses the
  current Legacy BIOS/IDE raw image path; this does not implement UEFI, OVMF,
  ESP/FAT images, virtio, AHCI/SATA, NVMe, or new storage drivers.
- Prefer QEMU with `--display none` for automated smoke tests, serial marker
  checks, and CI-like local validation, using either the `xmake run qemu -- --display none`
  wrapper or the equivalent Python helper path. Prefer graphical QEMU for quick
  local boot validation when Bochs-specific debugging is not required.
- Use Bochs, or Bochs/QEMU cross-validation when available, for early boot,
  BIOS, real-mode/protected-mode/long-mode transition, ATA PIO, interrupt, port
  IO, or hardware-behavior investigations.
- Check local QEMU, Bochs ROM/display, and cross-toolchain availability before
  assuming emulator runs are portable.
- `bigos.py` contains placeholder tasks. Do not assume it provides complete
  build/install automation.

## Testing And Validation

- For source changes, run the narrowest useful build or static check available.
- For boot, linker, memory, IRQ, or driver changes, prefer an emulator smoke test
  when the environment is configured.
- For automated emulator smoke, serial marker checks, and CI-like local
  validation, prefer the QEMU headless helper path, for example `uv run python
  tools/boot_debug.py run --emulator qemu --display none ...`.
- For quick local boot validation, prefer `xmake run qemu` or the equivalent
  graphical QEMU helper path unless Bochs-specific debugging is needed.
- For early boot, BIOS, real-mode/protected-mode/long-mode transition, ATA PIO,
  IRQ/timer/syscall, port IO, or low-level driver behavior, use Bochs review or
  Bochs/QEMU cross-validation when the environment supports it.
- If QEMU, Bochs, cross-binutils, display/ROM dependencies, disk image paths, or
  local emulator configuration are unavailable, explicitly record the missing
  tool or configuration, skipped validation, substitute checks, and residual
  risk instead of claiming runtime validation.
- After documentation-only changes, syntax checks are enough when applicable.
- For documentation-only changes, run OpenSpec parse/status checks and targeted
  consistency searches; runtime build, clang/clangd, QEMU smoke, and Bochs
  cross-validation are not required unless source, build, or helper files change.
- For Python-related validation or helper execution, use `uv run ...` such as
  `uv run pytest`, `uv run python <script>`, `uv run ruff check`, and
  `uv run pyright`. If `uv` is unavailable, record that explicitly instead of
  falling back silently.

## Low-Level Risk Areas

- Boot flow: `src/arch/x86/boot/boot.s`, `src/arch/x86/boot/boot.cc`,
  `link.lds`.
- Memory initialization order: `src/mm/kmem.cc`, `src/mm/buddy.cc`,
  `src/mm/vmem.cc`, `src/mm/slab.cc`, `src/mm/memory.cc`.
- Memory API layering (page-count vs buddy-order semantics) and the early
  metadata arena used during buddy bootstrap: do not reintroduce removed
  mixed-semantics aliases or make bootstrap depend on dynamic slab growth.
- Interrupt descriptors and ISR calling convention: `src/kernel/irq/interrupt.s`,
  `src/kernel/irq/interrupt.cc`, `include/irq/interrupt.h`. Keep the kernel-owned
  static IDT, the `InterruptFrame` layout, and the exception-vs-IRQ EOI split.
  The syscall IDT gate (`int 0x80`) is DPL=3 while exception/IRQ gates stay
  ring0-only; the syscall path must not send an i8259 EOI.
- Timer and scheduler context switch: `src/kernel/timer/timer.cc`,
  `src/drivers/timer/pit.cc`, `src/kernel/sched/sched.cc`,
  `src/kernel/sched/switch.s`. Keep `timer::on_tick()` IRQ-context-safe, keep
  scheduler wait/sleep lists allocation-free in IRQ context, preserve
  preemption-disable and IRQ-return reschedule guards, and preserve the
  callee-saved context-switch frame layout and idle-thread ownership of halt.
- Syscall entry and user mode: `src/kernel/syscall/syscall.cc`,
  `src/kernel/proc/proc.cc`, `src/kernel/proc/user_mode.cc`,
  `src/kernel/proc/user_mode.s`. Preserve the minimal syscall ABI, the GDT/TSS
  and RSP0 setup, the `iretq` ring3 entry, and explicit CR3 switching; never
  access unmapped VGA/MMIO under a user CR3.
- Process, fd/VFS, and user-memory boundaries: `include/bigos/proc.h`,
  `include/bigos/syscall.h`, `include/bigos/fs/vfs.h`, `src/kernel/fs/vfs.cc`,
  and `src/kernel/proc/proc.cc`. Keep process lifecycle, fd table, `brk`,
  restricted anonymous mapping, and VMA-backed user-buffer validation bounded;
  do not imply POSIX `fork`, writable files, page cache, broad `mmap`, demand
  paging, COW, or user-space libc.
- Driver port IO and hardware state: `src/drivers/video/vga.cc`,
  `src/drivers/irqchip/i8259.cc`, `src/drivers/timer/pit.cc`,
  `src/kernel/bigos/io.cc` (VGA and COM1 serial).
- C++ runtime behavior: `cpp/libsupc++`, `cpp/ktl`, global constructors, and
  `new/delete`.

## Current Maturity

- Boot, VGA/serial output, kernel-owned IDT and exception/IRQ/syscall dispatch,
  PIC, PIT timer IRQ0 with a monotonic tick, keyboard decode plus TTY/console
  input, buddy allocation with an early metadata arena, slab allocation (reclaim,
  large allocations, debug guards, stats), kernel virtual memory, owned empty
  page-table reclamation, and the kernel direct map are implemented and
  exercised by switchable runtime smokes.
- A single-core round-robin kernel-thread scheduler with cooperative switching,
  wait queues, timeout sleep, preemption-disable depth, and guarded IRQ-return
  timer preemption is implemented. The `int 0x80` syscall entry supports the
  minimal diagnostic/user ABI plus bounded process `wait`, read-only fd/VFS
  `open`/`read`/`close`, `brk`, and restricted anonymous mapping.
- The process lifecycle core, safe user-process teardown/reaping, process-local
  fd table, VMA-backed user-buffer validation, read-only VFS/exFAT path, flat
  first-user-program smoke, and filesystem-backed ELF64 user-program smoke are
  implemented with explicit bounded/default-off smoke boundaries where
  applicable.
- Not yet implemented: SMP, a full POSIX multi-process model, `fork`, COW,
  general demand paging, broad `mmap` including file-backed mappings,
  user-space libc, writable filesystems, page cache, broad storage/device
  drivers, a UEFI backend, and release-grade CI automation.
- Some code paths are scaffolding or TODOs. Inspect call sites before assuming a
  subsystem is wired into the normal boot path; smoke entry threads and runtime
  validation markers remain gated by build switches even when their shared
  subsystems are compiled normally.

## Collaboration Guidelines

- Before implementing a feature, identify the affected subsystem and initialization
  order.
- For OpenSpec work, include architecture assumptions, memory layout assumptions,
  emulator/toolchain assumptions, and explicit non-goals.
- For OpenSpec work, write change artifacts in Chinese by default, including
  proposal, design, specs, tasks, validation notes, and documentation tasks,
  while preserving OpenSpec-required structural keywords when needed for tooling.
- For reviews, focus on correctness, undefined behavior, bootability, memory
  safety, interrupt safety, and initialization order.
- For documentation sync, keep Stage 9-14 bounded capabilities aligned with
  `roadmap.md`, current source, and archived OpenSpec specs without describing
  BigOS as a complete POSIX or general-purpose OS.
- When editing `docs/en` or `docs/zh`, update the corresponding language mirror
  in the same change and keep the directory structures isomorphic.
- Do not revert unrelated local changes.
