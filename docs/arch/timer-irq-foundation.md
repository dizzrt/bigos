# Timer IRQ Foundation

BigOS uses the legacy PIT 8253/8254 channel 0 as the first early periodic timer
source on the current BIOS + i8259 path.

## Scope

- Programs PIT channel 0 through data port `0x40` and command port `0x43`.
- Uses `1193182 Hz` as the PIT input frequency and `100 Hz` as the initial
  early-kernel target tick rate.
- Registers an IRQ0 handler for vector `0x20` before unmasking IRQ0 during
  ordinary boot.
- Maintains a single-core early-kernel tick counter exposed through
  `bigos::timer::ticks()`.
- Provides `bigos::timer::mdelay()` as a coarse busy-wait helper.

## Non-Goals

This foundation does not introduce a scheduler, preemption, blocking sleep,
timer queues, APIC/IOAPIC, HPET, TSC calibration, SMP tick accounting, or
user-visible time APIs.

`mdelay()` is not scheduler sleep. It does not yield, does not block on a wait
queue, and only has coarse timing semantics after PIT IRQ0 delivery is enabled.

## Validation Smoke

`timer_smoke` is disabled by default. Ordinary boot still unmasks IRQ0, but does
not emit periodic timer output. When enabled, `BIGOS_TIMER_SMOKE` emits
`BIGOS_TIMER_IRQ` only for the first few timer ticks, keeping ordinary boot logs
free from unbounded periodic output.

The timer handler does not send PIC EOI directly. EOI remains owned by the
external IRQ dispatch boundary after the registered handler returns.
