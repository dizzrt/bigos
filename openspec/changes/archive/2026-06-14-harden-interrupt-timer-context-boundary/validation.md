# 实现与验证记录

## Boundary Inventory

- `kernel/core/irq` owns IDT setup, ISR stubs, exception/external IRQ/syscall dispatch, page-fault diagnostics or user-fault handoff, unknown-vector diagnostics, and the single external i8259 EOI point.
- `kernel/drivers/irqchip` and `kernel/drivers/timer` own PIC/PIT port IO, mask/unmask, spurious IRQ handling, EOI primitive, PIT mode programming, and the current 100 Hz legacy timer source.
- `kernel/core/timer` owns monotonic tick state through `timer::on_tick()` and `timer::ticks()`; IRQ0 advances ticks only through the timer-owned API.
- `kernel/core/sched` owns TCB lifetime, run/wait/sleep queues, scheduler critical sections, preemption-disable depth, bounded reschedule intent, and safe-boundary context switching.
- `kernel/core/sched/switch.s` owns the current x86_64 callee-saved register and stack frame mechanics; ordinary scheduler policy now reaches it through `bigos::arch_context::switch_kernel_context()`.

## Preserved Assumptions

- Interrupt vectors, `VECTOR_SYSCALL = 0x80`, CPU exception behavior, i8259 vector range, and syscall no-EOI behavior are unchanged.
- External IRQ dispatch still sends exactly one i8259 EOI after the registered handler returns; CPU exceptions and `int 0x80` send no EOI.
- `InterruptFrame` field order, size, offsets, ISR register-save order, and `iretq` return path are unchanged.
- The context-switch assembly frame layout and global `switch_context` symbol remain unchanged; only the scheduler-facing call boundary moved behind `include/bigos/arch_context.h`.
- Boot/linker addresses, page-table layout, disk image layout, and user-visible syscall ABI were not modified.
- The runnable backend remains the current single-core x86_64 Legacy BIOS/MBR/exFAT path; this change does not add SMP, UEFI runtime parity, non-x86 runtime parity, APIC/IOAPIC, HPET, a complete HAL, POSIX scheduling, or real-time semantics.

## Source Review

- Added `include/bigos/arch_context.h` as the narrow scheduler-facing architecture-context header. It exposes semantic helpers for IRQ-return context classification and a kernel context-switch primitive only.
- Added `kernel/core/sched/arch_context.cc` to keep the raw x86_64 `InterruptFrame::cs` RPL check and assembly `switch_context` symbol outside ordinary scheduler policy.
- Updated `kernel/core/sched/sched.cc` so IRQ-return preemption eligibility uses `arch_context::is_kernel_irq_return_context()` and every scheduler switch call uses `arch_context::switch_kernel_context()`.
- Reviewed `kernel/core/irq/isr.cc`: PIT IRQ0 still calls `timer::on_tick()` and bounded `sched::on_timer_tick()` only; it does not send EOI or switch directly.
- Reviewed `kernel/core/sched/sched.cc`: `on_timer_tick()` remains allocation-free, nonblocking, no bulk IO, no filesystem access, no delay loops, and records bounded reschedule intent for safe-boundary handling.
- Reviewed PIC/PIT driver ordering: PIT programming remains driver-owned; `i8259::send_eoi()` remains the EOI primitive and is called from external IRQ dispatch only.

## Targeted Searches

- `send_eoi(` in source found only `kernel/core/irq/interrupt.cc`, `kernel/drivers/irqchip/i8259.cc`, and `include/drivers/irqchip/i8259.h`; external IRQ dispatch remains the only source call site that sends EOI.
- `switch_context(` in source found only `kernel/core/sched/arch_context.cc` and `kernel/core/sched/switch.s`; ordinary scheduler policy no longer declares or calls the assembly symbol directly.
- Raw `InterruptFrame` RPL checks remain in architecture-owned interrupt/proc/signal paths and the new `arch_context` implementation; scheduler preemption no longer open-codes `InterruptFrame::cs`.
- Change-local search for stage-number wording found stale design text, which was replaced with “本变更”.
- Scope search confirmed this change records SMP, UEFI runtime parity, non-x86 runtime parity, APIC/IOAPIC, HPET, complete HAL, POSIX scheduling, and real-time semantics only as non-goals or forbidden claims.

## Validation Results

- `openspec status --change harden-interrupt-timer-context-boundary`: passed; all artifacts complete.
- `openspec validate harden-interrupt-timer-context-boundary --strict`: passed.
- `xmake`: passed after adding `include/bigos/arch_context.h` and `kernel/core/sched/arch_context.cc`.
- IDE diagnostics: no diagnostics in `include/bigos/arch_context.h`, `kernel/core/sched/arch_context.cc`, `kernel/core/sched/sched.cc`, `docs/en/arch/architecture-core-boundaries.md`, or `docs/zh/arch/architecture-core-boundaries.md`.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 30`: passed; QEMU observed `BIGOS_USER_EXEC`.
- QEMU build phase emitted existing assembler warnings for `movsd` in boot-sector sources; they are not introduced by this boundary change.
- No Python helper or test files were modified; `uv run ruff`, `uv run pyright`, and `uv run pytest` were not required for this change.
- Bochs cross-validation was not run because the behavior-preserving boundary wrapper passed the default QEMU headless smoke; remaining Bochs-only risk is limited to optional manual/early hardware-behavior cross-check coverage.
