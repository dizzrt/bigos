# Agent Guide For BigOS

This repository contains BigOS, an early-stage x86_64 freestanding operating
system kernel. Treat it as low-level kernel code, not as a hosted application.

## Project Context

- BigOS is written primarily in C++17, C17, and x86/x86_64 assembly.
- xmake is the primary build system.
- `x86_64-elf-gcc` is the expected cross toolchain.
- Bochs is used for local emulation.
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
  - `src/kernel/sched`: cooperative scheduler and context-switch assembly.
  - `src/kernel/syscall`: `int 0x80` dispatcher and ABI.
  - `src/kernel/proc`: minimal process model and ring3 user-mode entry
    (compiled only under `user_program_smoke`).
  - `src/kernel/bigos`: low-level IO, panic, and utility helpers.
- `src/mm`: buddy allocator, slab allocator, `kmalloc/free`, virtual memory, and
  the kernel direct map.
- `src/drivers`: VGA text mode, i8259 PIC, and PIT timer drivers.
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
xmake run kernel
make run
```

Validation build switches (all off by default, see `xmake.lua`):

```bash
xmake f --mm_self_test=y       # BIGOS_MM_SELF_TEST: early memory runtime self-test
xmake f --slab_debug=y         # BIGOS_SLAB_DEBUG: slab debug guards (implied by mm_self_test)
xmake f --page_fault_smoke=y   # BIGOS_PAGE_FAULT_SMOKE -> BIGOS_PAGE_FAULT
xmake f --timer_smoke=y        # BIGOS_TIMER_SMOKE -> BIGOS_TIMER_IRQ
xmake f --keyboard_smoke=y     # BIGOS_KEYBOARD_SMOKE: enables the IRQ1 line
xmake f --scheduler_smoke=y    # BIGOS_SCHEDULER_SMOKE -> BIGOS_SCHED_THREAD_A/B
xmake f --user_vmem_smoke=y    # BIGOS_USER_VMEM_SMOKE -> BIGOS_USER_VMEM_SMOKE_PASSED/FAILED
xmake f --syscall_smoke=y      # BIGOS_SYSCALL_SMOKE -> BIGOS_SYSCALL_SMOKE_PASSED/FAILED
xmake f --user_program_smoke=y # BIGOS_USER_PROGRAM_SMOKE -> BIGOS_USER_ENTER/EXIT
```

`--user_program_smoke` additionally compiles `src/kernel/proc/**` and enters the
first ring3 user program; it is not part of a normal boot.

For bounded emulator smoke against memory markers:

```bash
uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
uv run python tools/boot_debug.py run --user-program-smoke
```

The self-test emits `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED`,
the `#PF` handler emits `BIGOS_PAGE_FAULT`, and the unified panic path emits
`BIGOS_PANIC code=<code> source=<source>` on COM1 and VGA. `make boot-debug-user-gui`
launches the GUI Bochs flow with the first-user-program smoke enabled.

Notes:

- Verify that `x86_64-elf-gcc`, `x86_64-elf-g++`, `x86_64-elf-ld`, and related
  binutils are installed before building.
- Bochs configuration may contain host-specific paths. Check `test/bochsrc.bxrc`
  before assuming emulator runs are portable.
- `bigos.py` contains placeholder tasks. Do not assume it provides complete
  build/install automation.

## Testing And Validation

- For source changes, run the narrowest useful build or static check available.
- For boot, linker, memory, IRQ, or driver changes, prefer an emulator smoke test
  when the environment is configured.
- If Bochs or the cross toolchain is unavailable, report that clearly instead of
  claiming runtime validation.
- After documentation-only changes, syntax checks are enough when applicable.
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
  `src/kernel/sched/switch.s`. Keep `timer::on_tick()` IRQ-context-safe, keep the
  scheduler cooperative (no preemption on IRQ return), and preserve the
  callee-saved context-switch frame layout and idle-thread ownership of halt.
- Syscall entry and user mode: `src/kernel/syscall/syscall.cc`,
  `src/kernel/proc/proc.cc`, `src/kernel/proc/user_mode.cc`,
  `src/kernel/proc/user_mode.s`. Preserve the minimal syscall ABI, the GDT/TSS
  and RSP0 setup, the `iretq` ring3 entry, and explicit CR3 switching; never
  access unmapped VGA/MMIO under a user CR3.
- Driver port IO and hardware state: `src/drivers/video/vga.cc`,
  `src/drivers/irqchip/i8259.cc`, `src/drivers/timer/pit.cc`,
  `src/kernel/bigos/io.cc` (VGA and COM1 serial).
- C++ runtime behavior: `cpp/libsupc++`, `cpp/ktl`, global constructors, and
  `new/delete`.

## Current Maturity

- Boot, VGA/serial output, kernel-owned IDT and exception/IRQ/syscall dispatch,
  PIC, PIT timer IRQ0 with a monotonic tick, keyboard decode plus TTY/console
  input, buddy allocation with an early metadata arena, slab allocation (reclaim,
  large allocations, debug guards, stats), kernel virtual memory, and the kernel
  direct map are implemented and exercised by switchable runtime smokes.
- A cooperative (non-preemptive) kernel-thread scheduler, the `int 0x80` syscall
  entry with a minimal ABI, and a default-off first ring3 user-program smoke
  (SYS_WRITE/SYS_EXIT closed loop) are implemented with explicit smoke-only
  boundaries.
- Not yet implemented: preemptive scheduling, a full multi-process model,
  ELF user-program loading (the smoke uses a flat embedded image), demand
  paging/COW/mmap/brk, empty page-table reclamation, in-kernel block/FS
  services, broad device drivers, a UEFI backend, and CI automation.
- Some code paths are scaffolding or TODOs. Inspect call sites before assuming a
  subsystem is wired into `kernel()`; smokes and the proc subsystem are gated by
  build switches (the proc subsystem only compiles under `user_program_smoke`).

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
- When editing `docs/en` or `docs/zh`, update the corresponding language mirror
  in the same change and keep the directory structures isomorphic.
- Do not revert unrelated local changes.
