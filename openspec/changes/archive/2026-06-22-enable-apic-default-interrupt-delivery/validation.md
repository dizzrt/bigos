## Validation Notes

### ABI And Layout Review

- Syscall ABI unchanged: vector `0x80`, register argument convention, return in
  `rax`, and syscall no-irqchip-EOI ownership remain unchanged.
- Process, signal, resident init, shell, fd/VFS, and bounded userland behavior are
  not intentionally changed by this work. IRQ-return signal delivery remains in
  `kernel/core/irq/interrupt.cc` after the scheduler IRQ-return bridge.
- Time/timer visible behavior remains bounded to the existing monotonic tick API.
  APIC default delivery changes the runtime scheduler tick owner to LAPIC timer
  when the APIC path is active; documented PIC/PIT fallback remains BSP-only.
- Layout assumptions unchanged: kernel link address, AP trampoline range,
  boot handoff, GDT/TSS setup, page-table layout, direct map, page-table
  self-mapping, disk image layout, and user address-space layout are not changed.
- No user-visible breaking ABI or layout change is introduced by this change.

### Source-Level Checks

The implementation adds or updates checks for:

- Vector ownership and owner names in `include/irq/interrupt.h` and
  `kernel/core/irq/interrupt.cc`.
- EOI uniqueness: PIC-owned fallback IRQs use i8259 EOI; LAPIC/APIC-owned timer,
  IPI, and IOAPIC IRQs use LAPIC EOI; CPU exceptions and syscall send no irqchip
  EOI.
- IOAPIC redirection fields: vector, mask, trigger mode, polarity, and target
  APIC ID.
- APIC fallback gating: `kernel/core/kernel.cc` starts APs only after
  `irq::apic_default_delivery_active()` is true; otherwise it records
  `BIGOS_APIC_DEFAULT_BSP_ONLY_FALLBACK`.
- Hard IRQ non-blocking constraints for timer, keyboard, and LAPIC/APIC dispatch.

### Commands

- `openspec validate enable-apic-default-interrupt-delivery --strict`: passed.
- `uv run pytest tests/test_apic_default_interrupt_delivery_source.py tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py tests/test_multicore_scheduler_source.py tests/test_tty_console_input_source.py tests/test_boot_debug.py`: 71 passed.
- `uv run pytest tests/test_bilingual_docs_layout.py`: 4 passed.
- `xmake f --scheduler_smp_smoke=y && xmake`: passed.
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -DBIGOS_USER_PROCESS -DBIGOS_AP_STARTUP_PERCPU_TIMERS -DBIGOS_SCHEDULER_SMP_SMOKE -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/irq/interrupt.cc kernel/core/irq/isr.cc kernel/core/kernel.cc kernel/drivers/irqchip/ioapic.cc`: passed.
- `uv run python tools/boot_debug.py runtime-smoke-matrix --case apic-default-interrupt-delivery`: passed; QEMU observed `BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE`.
- `xmake f --scheduler_smp_smoke=y && uv run python tools/boot_debug.py run --emulator bochs --display none --bochs-cpus 2 --serial-log build/test/apic-default-bochs.serial.log --expect-serial-marker BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE --smoke-timeout 20 --image build/test/apic-default-bochs.raw`: passed; Bochs observed `BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE`.

### Runtime Validation Boundary

QEMU multi-core headless smoke and Bochs 2-CPU no-GUI smoke both observed
`BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE`. The runtime checks validate the bounded
supported APIC default delivery gate for LAPIC timer ownership, IOAPIC keyboard
routing setup, LAPIC EOI ownership, and AP startup gating in the local emulator
configuration. They do not claim CPU hotplug, NUMA, MSI/MSI-X, complete IRQ
affinity/load balancing, broad device IRQ migration, non-x86_64 backend parity,
or release-grade CI coverage.
