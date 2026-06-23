# Memory Runtime Validation

BigOS can run an optional early memory runtime self-test after `init_mem()` and before IRQ/PIC setup. The self-test is intended only for emulator validation builds and is disabled by default.

## Enabling

- Configure with `xmake f --mm_self_test=y`, then run `xmake` or a local emulator target such as `xmake run qemu`.
- To generate and validate a raw image without launching an emulator, run `uv run python tools/boot_debug.py run --no-launch --serial-log logs/serial.log`.
- For bounded automated smoke validation, prefer the QEMU headless helper path: `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`.
- For Bochs cross-checking, run `uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log logs/bochs.serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED` when local Bochs ROM/display configuration is available.

## Markers

- Success marker: `BIGOS_MM_SELF_TEST_PASSED`
- Failure marker: `BIGOS_MM_SELF_TEST_FAILED stage=<stage>`

Success and failure markers are written to COM1 and VGA. On failure, the CPU is safely paused through `hlt`.

## Coverage

The self-test covers representative `kmalloc/free` size classes, mapped kernel virtual page allocation, direct low-order physical buddy allocation, and a controlled buddy page's kernel direct-map conversion plus read/write access. The direct-map check confirms that `phys_to_direct()` / `direct_to_phys()` are reversible, out-of-range physical addresses return an explicit failure value, `KVMEM_BASE` is not treated as a direct-map address, and buddy statistics are restored after the physical page is freed. It does not enable IRQs, scheduler, SMP, filesystem services, user mode, or hosted runtime APIs.

## Interrupt-Context Contract

kernel memory API capability defines context boundaries for ordinary allocator entries, but does not upgrade them to IRQ-handler-safe APIs:

- `kmalloc()`, `free()`, `alloc_kernel_pages()`, `free_pages()`, and global `new/delete` are non-IRQ-handler-safe APIs.
- `alloc_kernel_pages()` keeps page-count semantics. Internal buddy `alloc_physical_order()` keeps order semantics. Do not add `alloc_pages()`, `alloc_physical_pages()`, or `free_physical_pages()` aliases.
- `collect_slab_stats()`, `g_nr_free_pages()`, and `kernel_vmem_free_pages()` are IRQ-disabled-only snapshots. They briefly mask same-CPU maskable IRQ interleaving while reading allocator statistics.
- `print_slab_stats()` and `print_physical_memory_info()` are non-interrupt-context-only diagnostic output helpers and should not be called from IRQ handlers.

The added `bigos::irq::InterruptGuard` saves entry `RFLAGS.IF`, executes `cli` on entry, and executes `sti` on exit only if IF was 1 on entry. The guard protects only single-core same-CPU maskable IRQ interleaving. It does not provide SMP mutual exclusion, NMI protection, blocking semantics, or scheduler-lock semantics.

Allocator internals use guarded regions only around metadata boundaries such as buddy free-list/`PageBlock` accounting, slab cache lists/bitmaps/large-allocation accounting, KVMEM free/used lists, physical backing records, PTE writes/clears, and TLB invalidation bookkeeping. Guarded regions do not contain `mdelay()`, filesystem, scheduler, user-mode interactions, or bulk console/serial output. Spinlock, preemption, and sleepable allocation boundaries must still be redesigned before scheduler/SMP work.

## Validation Boundary

Source-level validation covers API annotations, interrupt guard IF save/restore, forbidden ISR allocator tokens, and `mm_self_test()` initialization order. Cross builds or `-fsyntax-only` checks confirm freestanding C++/assembly changes are accepted by the target configuration.

QEMU headless runtime smoke is the preferred automated serial-marker path for this Legacy BIOS/MBR/exFAT image. Bochs remains useful for early boot, BIOS, ATA PIO, interrupt, and hardware-behavior cross-checking. If QEMU, Bochs, cross-binutils, ROM/display configuration, disk image paths, or serial-marker observation are unavailable, validation needs to record why it was not run, which substitute checks passed, and the remaining bootability risk.
