## ADDED Requirements

### Requirement: Timer tick update uses a controlled IRQ-context API

BigOS SHALL expose a controlled IRQ-context-safe timer internal API that the IRQ0 handler uses to advance the monotonic tick, instead of the IRQ layer directly mutating timer-internal tick state.

#### Scenario: IRQ0 handler advances tick through on_tick

- **WHEN** i8259 IRQ0 fires after interrupts are enabled
- **THEN** the timer IRQ handler calls a timer-owned API such as `bigos::timer::on_tick()` to advance the monotonic kernel tick
- **AND** the timer IRQ handler MUST NOT directly mutate timer-internal tick storage such as `bigos::timer::__detail::g_ticks`

#### Scenario: Tick state ownership lives in the timer translation unit

- **WHEN** timer tick state is defined
- **THEN** the monotonic tick counter and its update logic are owned by the timer translation unit
- **AND** the IRQ layer interacts with tick state only through the timer API

#### Scenario: on_tick stays freestanding and IRQ-safe

- **WHEN** the timer `on_tick()` API runs in IRQ context
- **THEN** it only advances the monotonic tick counter
- **AND** it MUST NOT allocate memory, block, call `kprintf`, send i8259 EOI, depend on scheduler, TTY, or filesystem, or use hosted runtime APIs

### Requirement: Timer API context contract is explicit

BigOS SHALL document and enforce which timer APIs may be called in IRQ context versus non-interrupt context under the current single-core early-kernel assumptions.

#### Scenario: on_tick is IRQ-context only

- **WHEN** kernel code calls the timer `on_tick()` API
- **THEN** the call site is the timer IRQ handler in IRQ context
- **AND** `on_tick()` MUST NOT be called from ordinary non-interrupt kernel code paths

#### Scenario: ticks is a context-agnostic read

- **WHEN** kernel code reads the timer tick API
- **THEN** it returns a monotonic tick snapshot valid under the current single-core early-kernel model
- **AND** the read makes no guarantee about SMP coherence or high-resolution timing

#### Scenario: mdelay is non-interrupt context only

- **WHEN** kernel code calls a busy-wait delay primitive such as `mdelay()`
- **THEN** the call occurs in non-interrupt context with maskable interrupts enabled so that IRQ0 keeps advancing ticks
- **AND** the API documentation states that calling it with interrupts disabled or inside an IRQ handler busy-waits forever
- **AND** source-level checks confirm that `mdelay()` and tick polling do not appear inside any ISR handler body

### Requirement: Timer runtime path is validated under a stable oracle

BigOS SHALL validate the hardened timer/IRQ runtime path with source-level checks and bounded emulator smoke when available, without introducing scheduler, SMP, or user-mode dependencies.

#### Scenario: Source-level checks cover the controlled tick API

- **WHEN** this change is implemented
- **THEN** tests or static checks confirm `bigos::timer::on_tick()` exists and is called by the timer IRQ handler
- **AND** tests or static checks confirm the timer IRQ handler no longer directly mutates `g_ticks`
- **AND** tests or static checks confirm tick state is defined in the timer translation unit

#### Scenario: Emulator smoke confirms periodic ticks

- **WHEN** Bochs, ROM paths, generated disk image, serial oracle, and the timer smoke switch are available
- **THEN** validation boots with timer smoke enabled and observes the bounded `BIGOS_TIMER_IRQ` marker, confirming IRQ0 periodic delivery, EOI, and `iretq` return
- **AND** validation confirms the monotonic tick advances across the observed period

#### Scenario: Runtime smoke unavailable

- **WHEN** the emulator, ROM, cross toolchain, or serial oracle is unavailable
- **THEN** validation records the missing dependency, the source and build checks that passed, and the remaining IRQ0 runtime risk
