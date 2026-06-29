# Timer IRQ Foundation

BigOS 使用 legacy PIT 8253/8254 channel 0 作为最早期 periodic timer source，也作为
Legacy BIOS/i8259 fallback。APIC-backed timer ownership 由 scheduler 与 multicore
validation 路径记录。

## Scope

- Programs PIT channel 0 through data port `0x40` and command port `0x43`.
- Uses `1193182 Hz` as the PIT input frequency and `100 Hz` as the initial
  early-kernel target tick rate.
- Registers an IRQ0 handler for vector `0x20` before unmasking IRQ0 during
  ordinary boot.
- 维护通过 `bigos::timer::ticks()` 暴露的 early-kernel monotonic tick counter。
- Provides `bigos::timer::mdelay()` as a coarse busy-wait helper.
- Provides `bigos::timer::sleep_for()` as a cooperative scheduler sleep helper
  for ordinary non-interrupt kernel threads.

## Tick Ownership And API Context Contract

Tick state ownership lives in the timer translation unit. The monotonic counter
`bigos::timer::__detail::g_ticks` is defined in `kernel/core/timer/timer.cc`, and
the IRQ layer interacts with it only through the timer API. The IRQ0 handler in
`kernel/core/irq/isr.cc` advances the tick by calling the timer-owned
`bigos::timer::on_tick()`; it does not mutate `g_ticks` directly.

The early timer APIs have explicit execution-context contracts:

- `on_tick()` is IRQ-context only. It is called from the timer IRQ0 handler and
  only increments the monotonic tick. It does not allocate, block, do IO, call
  `kprintf`, send i8259 EOI, call `kmalloc`/`free`/`alloc_kernel_pages`/
  `free_pages`/global `new/delete`, or depend on scheduler/TTY/filesystem
  services.
- `ticks()` 是 context-agnostic read。它返回有界 kernel time model 的 monotonic
  tick snapshot，不承诺 high-resolution timing 或完整 POSIX clock 语义。
- `mdelay()` is non-interrupt context only. It must run with maskable interrupts
  enabled so that IRQ0 keeps advancing ticks. Calling it with interrupts disabled
  or inside an IRQ handler busy-waits forever.
- `sleep_for()` is ordinary thread context only. It delegates to the scheduler
  wait model, blocks the current kernel thread until a tick deadline expires, and
  returns the scheduler timeout result. It rejects IRQ/exception/syscall/fatal
  and interrupts-disabled contexts through the scheduler blocking guard.

Source-level checks confirm that `mdelay()` and tick polling never appear inside
any ISR handler body.

## Cooperative Timeout Sleep

blocking primitives and timer ownership capability keeps tick ownership in the timer subsystem: IRQ0 advances time only by
calling `bigos::timer::on_tick()`, and timeout waits read the monotonic tick
through `bigos::timer::ticks()`. The scheduler owns waiter state and deadline
tracking through intrusive TCB links.

Expired sleepers are processed by the bounded IRQ-context-safe
`bigos::sched::on_timer_tick()` hook. The hook may make expired threads runnable
for a later cooperative scheduling point, but it does not allocate, free, block,
call `mdelay()`, access filesystem services, print bulk output, or switch
threads from IRQ return.

同一个 scheduler-owned hook 可以在普通线程 time slice 到期时记录 bounded timer
preemption intent。它不拥有 hardware acknowledgement 或 context switching；
external IRQ dispatch 负责发送 EOI，scheduler 只在文档化的 IRQ-return boundary
处理 pending switch。

## ISR ABI Runtime Invariants

The ISR entry path (`kernel/core/irq/interrupt.s`) preserves the following ABI
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

This foundation does not introduce HPET, TSC calibration, complete POSIX clock
APIs, or POSIX sleep policy. Timer-driven preemption is limited to the current
bounded scheduler boundary and does not claim real-time or POSIX scheduling
semantics.

`mdelay()` is not scheduler sleep. It does not yield, does not block on a wait
queue, and only has coarse timing semantics after PIT IRQ0 delivery is enabled.

## Validation Smoke

`timer_smoke` is disabled by default. Ordinary boot still unmasks IRQ0, but does
not emit periodic timer output. When enabled, `BIGOS_TIMER_SMOKE` emits
`BIGOS_TIMER_IRQ` only for the first few timer ticks, keeping ordinary boot logs
free from unbounded periodic output.

The timer handler does not send PIC EOI directly. EOI remains owned by the
external IRQ dispatch boundary after the registered handler returns.
