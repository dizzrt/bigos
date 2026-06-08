## Purpose

Harden the early BigOS timer/IRQ runtime path by routing monotonic tick updates through a controlled timer-owned IRQ-context API, making the context contract for each timer API explicit, and validating the runtime path under a stable oracle without introducing scheduler, SMP, or user-mode dependencies.

## Requirements

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

### Requirement: Timer tick may record scheduler intent without violating IRQ contracts

BigOS SHALL allow the early timer runtime path to interact with the single-core scheduler through bounded IRQ-context-safe accounting, reschedule intent, and stage 11 IRQ-return scheduling while preserving timer tick ownership and the `on_tick()` context contract.

#### Scenario: Timer IRQ keeps tick ownership in timer API

- **WHEN** i8259 IRQ0 fires after the scheduler is introduced
- **THEN** the timer IRQ handler MUST continue to advance the monotonic tick through the timer-owned IRQ-context API such as `bigos::timer::on_tick()`
- **AND** scheduler integration MUST NOT require the IRQ layer to directly mutate timer-internal tick storage

#### Scenario: Timer scheduler hook is bounded

- **WHEN** timer IRQ0 notifies the scheduler about a tick or time slice
- **THEN** the scheduler-facing hook MUST be bounded and IRQ-context-safe
- **AND** it MUST NOT allocate memory, free memory, block, call `mdelay()`, perform console/serial bulk output, access filesystem services, or depend on user-mode services

#### Scenario: Reschedule intent is separated from ordinary allocation

- **WHEN** the timer tick marks that the current thread should yield at a later cooperative or preemptive scheduling boundary
- **THEN** the decision state MUST be represented without ordinary dynamic allocation in the IRQ handler
- **AND** the timer path MUST preserve the current single-core interrupt, EOI, and return-state contracts

#### Scenario: Stage 11 may preempt on eligible IRQ return

- **WHEN** timer IRQ0 expires the current thread's time slice and the interrupt dispatch path reaches a documented safe return boundary
- **THEN** BigOS MAY perform a scheduler-owned context switch before returning to the interrupted kernel thread
- **AND** it MUST do so only when preemption is enabled, the current thread is eligible to be preempted, and the interrupt frame ABI and EOI ordering remain valid

### Requirement: Timer supports cooperative sleep and timeout waits
BigOS SHALL provide timer-backed sleep or timeout wait primitives that use the monotonic tick to wake blocked threads under the single-core cooperative scheduler model.

#### Scenario: Sleep wait records deadline
- **WHEN** ordinary non-interrupt kernel code requests a sleep or timeout wait for a bounded tick duration
- **THEN** BigOS MUST record a deadline based on the existing monotonic tick
- **AND** the current thread MUST become non-runnable until the deadline expires or another wakeup/cancellation transition occurs

#### Scenario: Expired sleeper is woken
- **WHEN** timer processing observes that a sleeping thread's deadline has expired
- **THEN** BigOS MUST transition that thread to runnable with a deterministic timeout result
- **AND** the transition MUST NOT require ordinary allocator APIs in IRQ context

### Requirement: Timer IRQ integration remains bounded
BigOS SHALL keep timer IRQ0 handling bounded and IRQ-context safe while supporting timeout waits.

#### Scenario: on_tick keeps IRQ-safe contract
- **WHEN** PIT IRQ0 fires after timeout wait support exists
- **THEN** the IRQ path MUST continue to advance the monotonic tick through the timer-owned API such as `bigos::timer::on_tick()`
- **AND** `on_tick()` and any timer IRQ scheduler hook MUST NOT allocate memory, free memory, block, call `mdelay()`, access filesystem services, depend on user-mode services, or perform bulk console/serial output

#### Scenario: Timer IRQ does not preemptively switch
- **WHEN** a timeout expires during timer IRQ0
- **THEN** the IRQ path MAY mark or wake the expired thread through a bounded IRQ-safe path
- **AND** it MUST NOT perform a full context switch on IRQ return in stage 10
- **AND** external IRQ EOI and `iretq` return semantics MUST remain unchanged

### Requirement: Timer preemption accounting is deterministic
BigOS SHALL make timer-driven scheduler accounting deterministic enough for source-level checks and bounded runtime smoke validation.

#### Scenario: Time slice accounting uses monotonic tick
- **WHEN** scheduler preemption accounting is enabled
- **THEN** BigOS MUST base slice expiry on the existing monotonic PIT tick or an equivalent timer-owned tick event
- **AND** it MUST NOT introduce a separate unsynchronized timer source for scheduler slices

#### Scenario: Disabled preemption records pending intent
- **WHEN** timer IRQ0 observes slice expiry while preemption is disabled or the current context is not eligible for IRQ-return switching
- **THEN** BigOS MUST record pending reschedule intent or an equivalent bounded state
- **AND** it MUST NOT perform the context switch from inside the protected region

#### Scenario: Timer smoke markers remain bounded
- **WHEN** timer-driven preemption smoke emits validation markers
- **THEN** marker emission MUST be bounded and default-off through the relevant smoke configuration
- **AND** the normal timer smoke marker semantics MUST remain unchanged outside scheduler semantics validation
