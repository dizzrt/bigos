# Agent Guide For BigOS

This repository contains BigOS, an early-stage x86_64 freestanding operating
system kernel. Treat it as low-level kernel code, not as a hosted application.

## Project Context

- BigOS is written primarily in C++17, C17, and x86/x86_64 assembly.
- xmake is the primary build system.
- `x86_64-elf-gcc` is the expected cross toolchain.
- Bochs is used for local emulation.
- Python files are helper scripts unless explicitly stated otherwise.
- OpenSpec uses `openspec/config.yaml` for project context.

## Important Directories

- `arch/x86/boot`: boot sectors, long-mode transition, ELF loader, disk install
  helper.
- `kernel`: kernel entry, IRQ, low-level IO, string functions, console/TTY
  skeletons.
- `mm`: buddy allocator, slab allocator, `kmalloc/free`, virtual memory code.
- `drivers`: VGA text mode and i8259 PIC drivers.
- `cpp`: kernel C++ support library, KTL containers, `new/delete`, ABI stubs.
- `include`: public kernel headers and small freestanding header subset.
- `lib/src`: startup assembly objects.
- `test`: Bochs config, VHD image, and low-level test snippets.

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
- Keep public headers small. Include only what is required.
- Avoid adding dependencies unless the user explicitly asks and the dependency is
  valid for a freestanding kernel.
- Prefer ASCII for source and documentation unless a file already has a clear
  reason to use non-ASCII.
- Do not silently change boot addresses, linker addresses, interrupt vectors,
  page-table self-mapping addresses, disk offsets, or ABI assumptions.

## Build And Run

Common commands:

```bash
xmake
xmake run kernel
make run
```

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

## Low-Level Risk Areas

- Boot flow: `arch/x86/boot/boot.s`, `arch/x86/boot/boot.cc`, `link.lds`.
- Memory initialization order: `mm/kmem.cc`, `mm/buddy.cc`, `mm/vmem.cc`.
- Interrupt descriptors and ISR calling convention: `kernel/irq/interrupt.s`,
  `kernel/irq/interrupt.cc`, `include/irq/interrupt.h`.
- Driver port IO and hardware state: `drivers/video/vga.cc`,
  `drivers/irqchip/i8259.cc`, `kernel/bigos/io.cc`.
- C++ runtime behavior: `cpp/libsupc++`, `cpp/ktl`, global constructors, and
  `new/delete`.

## Current Maturity

- Boot, VGA output, IDT stubs, PIC code, keyboard parsing, buddy allocation, slab
  allocation, and early virtual memory have partial implementations.
- TTY/console routing, scheduler, process model, user mode, system calls,
  filesystem services, and broad device support are not yet implemented.
- Some code paths are scaffolding or TODOs. Inspect call sites before assuming a
  subsystem is wired into `kernel()`.

## Collaboration Guidelines

- Before implementing a feature, identify the affected subsystem and initialization
  order.
- For OpenSpec work, include architecture assumptions, memory layout assumptions,
  emulator/toolchain assumptions, and explicit non-goals.
- For reviews, focus on correctness, undefined behavior, bootability, memory
  safety, interrupt safety, and initialization order.
- Do not revert unrelated local changes.
