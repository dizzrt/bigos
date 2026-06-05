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

## Tick Ownership And API Context Contract

Tick state ownership lives in the timer translation unit. The monotonic counter
`bigos::timer::__detail::g_ticks` is defined in `src/kernel/timer/timer.cc`, and
the IRQ layer interacts with it only through the timer API. The IRQ0 handler in
`src/kernel/irq/isr.cc` advances the tick by calling the timer-owned
`bigos::timer::on_tick()`; it does not mutate `g_ticks` directly.

The three early timer APIs have explicit execution-context contracts:

- `on_tick()` is IRQ-context only. It is called from the timer IRQ0 handler and
  only increments the monotonic tick. It does not allocate, block, do IO, call
  `kprintf`, send i8259 EOI, call `kmalloc`/`free`/`alloc_kernel_pages`/
  `free_pages`/global `new/delete`, or depend on scheduler/TTY/filesystem
  services.
- `ticks()` is a context-agnostic read. It returns a monotonic tick snapshot
  valid under the current single-core early-kernel model and makes no guarantee
  about SMP coherence or high-resolution timing.
- `mdelay()` is non-interrupt context only. It must run with maskable interrupts
  enabled so that IRQ0 keeps advancing ticks. Calling it with interrupts disabled
  or inside an IRQ handler busy-waits forever.

Source-level checks confirm that `mdelay()` and tick polling never appear inside
any ISR handler body.

## ISR ABI Runtime Invariants

The ISR entry path (`src/kernel/irq/interrupt.s`) preserves the following ABI
invariants without changing the `InterruptFrame` layout or register-save order:

- General-purpose registers are saved in reverse `InterruptFrame` field order
  (so they appear as `r15..rax` in memory) and restored in the mirrored order
  before returning through `iretq`.
- A synthetic zero error-code slot is pushed for vectors without a CPU-provided
  error code, keeping the frame layout identical to error-code vectors.
- The stack is aligned to 16 bytes (System V AMD64) before calling the C++
  dispatch entry.
- CPU exception vectors send no i8259 EOI. An external IRQ vector (including
  timer vector `0x20`) sends exactly one EOI through `irq_dispatch` after the
  registered handler returns, then returns via `iretq`.

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
