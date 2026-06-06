# Interrupt And Exception Foundation

BigOS' early x86_64 interrupt path currently covers only the single-core, Legacy BIOS, i8259 PIC, and Bochs validation scenarios. The foundation makes CPU exceptions, external IRQs, and the minimal keyboard smoke diagnosable; it does not imply a full scheduler, input subsystem, user mode, or page-fault recovery capability.

## Initialization Order

`kernel()` keeps this order:

```text
VGA clear
serial_init()
init_mem()
optional BIGOS_MM_SELF_TEST
optional BIGOS_USER_VMEM_SMOKE
terminal::init_tty()
irq::initIRQ()
optional BIGOS_PAGE_FAULT_SMOKE trigger
irq::enableIRQ()
normal boot marker
optional syscall / scheduler / user-program smoke
sched::start()  (idle thread owns halt; replaces the bare hlt loop)
```

`serial_init()` explicitly initializes COM1 on the default boot path, so ordinary serial markers and early diagnostics no longer depend implicitly on `BIGOS_MM_SELF_TEST`. `BIGOS_MM_SELF_TEST` still runs before PIC initialization and `sti`. Ordinary allocators, kernel APIs, and memory self-tests do not promise IRQ-context safety; `kmalloc()`, `free()`, `alloc_kernel_pages()`, `free_pages()`, and global `new/delete` must not be called from IRQ handlers.

## Interrupt Guard

`bigos::irq::InterruptGuard` is the minimal critical-section primitive for the early single-core kernel. Construction reads `RFLAGS.IF` and executes `cli`; destruction restores `sti` only when IF was enabled on entry, leaving already-disabled paths disabled. The guard prevents same-CPU maskable IRQ interleaving only. It is not an SMP lock, does not protect NMI, has no blocking semantics, and is not a scheduler lock.

Allocator internals use this guard around short metadata update boundaries in buddy, slab, and KVMEM. That does not make ordinary allocators IRQ-handler-safe APIs. Future IRQ producers that need handoff storage should still use static or boot-time-prepared bounded storage and document their overflow/drop policy.

## IDT Ownership

The runtime IDT uses kernel-owned static storage. `irq::initIRQ()` builds gate descriptors and executes a kernel-stage `lidt`. The runtime IDT no longer writes to, or depends on, the legacy low-address `IDT_BASE = 0x1000` backing.

This change does not alter these layouts or ABIs:

- Fixed boot addresses and Legacy BIOS handoff addresses.
- Linker higher-half base `0xffffffff80000000`.
- Kernel physical load base `0x1000000`.
- `BootInfoHeader*` entry ABI and BootInfo v1/v2 layout.
- Boot-stage page tables, self-mapping addresses, or direct-map plan.

## ISR ABI

All generated ISR entries enter one assembly common path. Vectors without a CPU error code push a synthetic zero error-code slot; vectors with a CPU error code preserve the original CPU-pushed value. The assembly path saves general-purpose registers, passes a unified frame to C++ `irq_dispatch(InterruptFrame*)`, restores registers symmetrically on IRQ paths that may return, and finishes with `iretq`.

The current `InterruptFrame.rsp` is the interrupted stack pointer computed for ring-0 interrupt entry. BigOS has not yet implemented a general user privilege transition in this foundation, so it does not declare full user `SS:RSP` semantics here.

## Dispatch Policy

`irq_dispatch()` separates vectors by range:

- `0x00..0x1f`: CPU exceptions; no PIC EOI is sent.
- `0x20..0x2f`: remapped i8259 external IRQs; an EOI is sent after the handler returns.
- Other vectors: emit deterministic unknown-vector diagnostics and return.

Unregistered external IRQs use a safe default handler that prints the vector/IRQ line and then sends EOI so the PIC is not wedged.

## Page Fault

The `#PF` handler is diagnostic-only. It reads `CR2`, emits the fixed `BIGOS_PAGE_FAULT` marker, fault address, raw error code, and decoded present/write/user/reserved-bit/instruction-fetch flags. It then enters a `cli; hlt` loop. It does not allocate, free, repair page tables, retry the faulting instruction, or claim demand-paging support.

The validation-only trigger is enabled with `xmake f --page_fault_smoke=y`. Default boot does not actively trigger `#PF`.

## Keyboard IRQ1 Handoff

Keyboard IRQ1 now performs controlled input handoff instead of printing a smoke marker directly in the ISR. Initialization first calls `terminal::init_tty()` to prepare the input ring, console flag, and keyboard decoder state; then `irq::initIRQ()` registers the vector `0x21` handler. Default boot keeps i8259 IRQ line 1 masked. Manual keyboard IRQ validation explicitly unmasks IRQ1 with `xmake f --keyboard_smoke=y`.

The handler reads exactly one scancode byte from PS/2 data port `0x60`, performs bounded set-1 decoding, and passes supported characters to the fixed-capacity TTY input buffer. It does not send i8259 EOI directly, call `kprintf()`/`kput()`, write VGA/serial, allocate/free memory, block, call `mdelay()`, or depend on filesystem, scheduler, syscall, user mode, or TTY consumer progress. The external IRQ dispatch still sends exactly one EOI after the handler returns.

This path is not a full input subsystem. Multiple TTYs, blocking read, shell, user-mode input, and complete keyboard layouts are left for later stages.

## Validation Record

Passed checks:

- Exception dispatch path contains no PIC EOI.
- Keyboard IRQ1 handler registration happens before IRQ1 unmask.
- Memory self-test remains before IRQ/PIC initialization and `enableIRQ()`.
- The `#PF` handler reads `CR2`, emits `BIGOS_PAGE_FAULT`, and does not allocate, repair page tables, or retry.
- Runtime IDT uses static storage and executes `lidt`.
- `uv run pytest`: 31 passed.
- `xmake`: default build passed, with existing command-line whitespace warning, `$(buildir)` deprecation warning, and RWX LOAD segment linker warning.
- `xmake f --mm_self_test=n --page_fault_smoke=y && xmake && xmake f --mm_self_test=n --page_fault_smoke=n`: validation-only `#PF` trigger build passed and the option was restored to default off.
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/kernel/irq/interrupt.cc src/kernel/irq/isr.cc src/kernel/kernel.cc src/drivers/irqchip/i8259.cc`: passed.
- IDE diagnostics: no diagnostics in `include/irq/interrupt.h`, `src/kernel/irq/interrupt.cc`, `src/kernel/irq/isr.cc`, or `src/kernel/kernel.cc`.

Runtime smokes not completed:

- Ordinary boot smoke: `uv run python tools/boot_debug.py run --serial-log build/test/serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 30` first failed because an existing Bochs process locked `build/test/os.raw`; an isolated image still did not produce the serial marker within 30 seconds.
- Memory self-test runtime smoke: `uv run python tools/boot_debug.py run --image build/test/mem-smoke.raw --serial-log build/test/mem-smoke.serial.log --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED --smoke-timeout 30` did not produce the serial marker within 30 seconds, so this local Bochs/terminal GUI/serial combination was not a reliable oracle.
- `#PF` runtime smoke: the trigger build was verified, but because ordinary and memory self-test serial smokes were not observable, `BIGOS_PAGE_FAULT` runtime marker success was not claimed.
- Keyboard IRQ1 runtime smoke: no manual Bochs keyboard input was performed; this change intentionally does not extend `tools/boot_debug.py` to inject keyboard input.

Remaining risk: source-level checks and cross builds cover the key static invariants for IDT/ISR/PIC/keyboard/#PF, but ordinary boot, page-fault halt, and manual keyboard IRQ delivery still need rechecking in a Bochs environment with stable VGA/serial observation.
