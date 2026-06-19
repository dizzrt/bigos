# Agent Guide For BigOS

This repository contains BigOS, an early-stage x86_64 freestanding operating
system kernel. Treat it as low-level kernel code, not as a hosted application.

## Project Context

- BigOS is written primarily in C++17, C17, and x86/x86_64 assembly.
- xmake is the primary build system.
- `x86_64-elf-gcc` is the expected cross toolchain.
- QEMU and Bochs are supported local emulators for the Legacy BIOS/MBR/exFAT path,
  which remains the default runnable baseline; x86_64 UEFI exists as a runnable
  non-parity boot backend spike.
- Python files are helper scripts unless explicitly stated otherwise.
- Run Python-related commands through `uv run` by default, including `pytest`,
  helper scripts, linting, formatting, and type checks.
- OpenSpec uses `openspec/config.yaml` for project context.

## Important Directories

- `kernel/arch/x86/boot`: boot sectors, long-mode transition, and ELF loader.
- `kernel/core`: kernel entry and subsystems:
  - `kernel/core/irq`: IDT, exception/IRQ/syscall dispatch, ISR stubs.
  - `kernel/core/timer`: monotonic tick and `mdelay`.
  - `kernel/core/terminal`: console output, keyboard scancode decode, TTY input.
  - `kernel/core/sched`: single-core scheduler, wait queues, timeout sleep,
    guarded IRQ-return preemption, and context-switch assembly.
  - `kernel/core/syscall`: `int 0x80` dispatcher and ABI.
  - `kernel/core/proc`: bounded process model, growable fd table, VMA/heap
    metadata, demand-zero/COW handling, `fork`, signals, identity/time state,
    ring3 user-mode entry, `execve`, resident PID-1 init launch, safe
    teardown/reaping, and bounded ELF64 user-program loading. Flat
    first-user-program, filesystem-backed user ELF, and userland runtime entries
    remain default-off smoke consumers.
  - `kernel/core/fs`: VFS shell over exFAT, RAM-backed `/rw`, persistent
    clean-sync `/rw`, cwd/relative path lookup, constrained rename, metadata,
    fd-backed open/read/write/close/lseek/fsync, writable `bigfs`, block buffer
    cache, and bounded file reads/writes.
  - `kernel/core/bigos`: low-level IO, panic, and utility helpers.
- `kernel/mm`: buddy allocator, slab allocator, `kmalloc/free`, virtual memory, and
  the kernel direct map, including owned empty page-table reclamation.
- `kernel/drivers`: VGA text mode, i8259 PIC, PIT timer, and ATA PIO block drivers.
- `kernel/runtime`: startup assembly source objects.
- `user`: freestanding user crt0/libc, PID-1 init, `/bin/sh`, small user
  binaries, and default-off userland smoke source.
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
- Use repository-relative paths for all file references in documentation,
  OpenSpec artifacts, tasks, comments, and review notes. Do not use or commit
  local machine-specific absolute paths or `file://` URLs for repository files.
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
xmake f --block_io_request_smoke=y # BIGOS_BLOCK_IO_REQUEST_SMOKE -> BIGOS_BLOCK_IO_REQUEST_PASSED/FAILED
xmake f --user_elf_smoke=y     # BIGOS_USER_ELF_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --demand_paging_smoke=y # BIGOS_DEMAND_PAGING_SMOKE -> BIGOS_DEMAND_PAGING_PASSED/FAILED
xmake f --fork_cow_smoke=y     # BIGOS_FORK_COW_SMOKE -> BIGOS_FORK_COW_PASSED/FAILED
xmake f --time_identity_smoke=y # BIGOS_TIME_IDENTITY_SMOKE -> BIGOS_TIME_IDENTITY_PASSED/FAILED
xmake f --signal_smoke=y       # BIGOS_SIGNAL_SMOKE -> BIGOS_SIGNAL_PASSED/FAILED
xmake f --writable_fs_smoke=y  # BIGOS_WRITABLE_FS_SMOKE -> BIGOS_WRITABLE_FS_PASSED/FAILED
xmake f --pipe_smoke=y         # BIGOS_PIPE_SMOKE -> BIGOS_PIPE_PASSED/FAILED
xmake f --userland_smoke=y     # BIGOS_USERLAND_SMOKE -> BIGOS_USERLAND_PASSED/FAILED
```

Normal boot packages `/boot/user/init.elf`, `/bin/sh`, and bounded `/bin/*`
programs, enters resident PID-1 init, and starts `/bin/sh`; QEMU headless default
validation observes `BIGOS_USER_EXEC`. `--user_program_smoke`,
`--user_elf_smoke`, and `--userland_smoke` select default-off user-program
validation paths in place of the normal init payload. `kernel/core/proc/**`,
`kernel/core/fs/**`, and `user/**` are normal bounded userland baseline
subsystems; smoke switches only select extra validation entry points or marker
programs.

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
  current Legacy BIOS/IDE raw image path by default; UEFI spike artifacts are
  separate and do not provide runtime parity, storage/device parity, virtio,
  AHCI/SATA, NVMe, or new storage drivers.
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

- Boot flow: `kernel/arch/x86/boot/boot.s`, `kernel/arch/x86/boot/boot.cc`,
  `link.lds`.
- Memory initialization order: `kernel/mm/kmem.cc`, `kernel/mm/buddy.cc`,
  `kernel/mm/vmem.cc`, `kernel/mm/slab.cc`, `kernel/mm/memory.cc`.
- Memory API layering (page-count vs buddy-order semantics) and the early
  metadata arena used during buddy bootstrap: do not reintroduce removed
  mixed-semantics aliases or make bootstrap depend on dynamic slab growth.
- Interrupt descriptors and ISR calling convention: `kernel/core/irq/interrupt.s`,
  `kernel/core/irq/interrupt.cc`, `include/irq/interrupt.h`. Keep the kernel-owned
  static IDT, the `InterruptFrame` layout, and the exception-vs-IRQ EOI split.
  The syscall IDT gate (`int 0x80`) is DPL=3 while exception/IRQ gates stay
  ring0-only; the syscall path must not send an i8259 EOI.
- Timer and scheduler context switch: `kernel/core/timer/timer.cc`,
  `kernel/drivers/timer/pit.cc`, `kernel/core/sched/sched.cc`,
  `kernel/core/sched/switch.s`. Keep `timer::on_tick()` IRQ-context-safe, keep
  scheduler wait/sleep lists allocation-free in IRQ context, preserve
  preemption-disable and IRQ-return reschedule guards, and preserve the
  callee-saved context-switch frame layout and idle-thread ownership of halt.
- Syscall entry and user mode: `kernel/core/syscall/syscall.cc`,
  `kernel/core/proc/proc.cc`, `kernel/core/proc/user_mode.cc`,
  `kernel/core/proc/user_mode.s`. Preserve the minimal syscall ABI, the GDT/TSS
  and RSP0 setup, the `iretq` ring3 entry, and explicit CR3 switching; never
  access unmapped VGA/MMIO under a user CR3.
- Process, fd/VFS, and user-memory boundaries: `include/bigos/proc.h`,
  `include/bigos/syscall.h`, `include/bigos/fs/vfs.h`, `kernel/core/fs/vfs.cc`,
  and `kernel/core/proc/proc.cc`. Keep process lifecycle, fd table, `brk`,
  restricted anonymous mapping, demand paging, bounded `fork`/COW, signals,
  time/identity, writable `/rw`, pipes/dup, userland runtime, and VMA-backed
  user-buffer validation bounded; do not imply a complete POSIX process model,
  broad file-backed `mmap`, dynamic linking, job control, full libc, persistent
  writable filesystems, async I/O, SMP, or broad storage/device support.
- Driver port IO and hardware state: `kernel/drivers/video/vga.cc`,
  `kernel/drivers/irqchip/i8259.cc`, `kernel/drivers/timer/pit.cc`,
  `kernel/core/bigos/io.cc` (VGA and COM1 serial).
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
  diagnostic/user ABI plus bounded process, fd/VFS, pipe/dup, time/identity,
  signal, `brk`, restricted anonymous mapping, and `execve` calls used by the
  minimal userland.
- The process lifecycle core, growable process/fd tables, safe user-process
  teardown/reaping, demand-zero materialization, bounded `fork`/COW, signal
  delivery, VMA-backed user-buffer validation, exFAT plus RAM-backed and
  persistent clean-sync writable `/rw` VFS paths, cwd/relative path resolution,
  constrained rename, bounded metadata, page/buffer cache, default-on PID-1
  init, minimal user crt0/libc, `/bin/sh`, packaged `/bin/*`, flat
  first-user-program smoke, filesystem-backed ELF64 user-program smoke, and
  `userland_smoke` are implemented with explicit bounded/default-off smoke
  boundaries where applicable.
- Not yet implemented: SMP, a complete POSIX process/job-control model, broad
  file-backed `mmap`, dynamic linking/shared libraries, a complete POSIX libc,
  persistent full writable filesystems beyond the bounded clean-sync `/rw`
  backend, async I/O, broad storage/device drivers, UEFI runtime parity/backend
  parity, and release-grade CI automation.
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
- For OpenSpec work, keep change names focused on the capability or behavior and
  do not include roadmap stage numbers in change names.
- For OpenSpec work, do not cite roadmap stage numbers or roadmap task numbers
  in new changes, archived changes, validation notes, or documentation updates.
  Use capability names, behavior names, implementation boundaries, or
  artifact-local checklist numbers instead.
- For reviews, focus on correctness, undefined behavior, bootability, memory
  safety, interrupt safety, and initialization order.
- For documentation sync, keep the current bounded userland baseline aligned
  with `roadmap.md`, current source, and archived OpenSpec specs without
  describing BigOS as a complete POSIX or general-purpose OS.
- Keep `roadmap.md` at project-planning level only: describe implemented
  capabilities, missing capabilities, medium/long-term planning, and staged
  development priorities. Do not add concrete entry points, file paths, commands,
  validation markers, source-level implementation details, or archive/version
  indexes to the roadmap; keep those details in dedicated docs, OpenSpec
  artifacts, source-adjacent notes, or validation records.
- When editing `docs/en` or `docs/zh`, update the corresponding language mirror
  in the same change and keep the directory structures isomorphic.
- Do not revert unrelated local changes.
