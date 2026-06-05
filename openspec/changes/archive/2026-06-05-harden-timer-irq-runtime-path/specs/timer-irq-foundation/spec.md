## MODIFIED Requirements

### Requirement: Timer IRQ0 handler is minimal and observable

BigOS SHALL register an early timer handler for i8259 IRQ0 that proves periodic external IRQ delivery without introducing scheduler, blocking, allocation, or hosted runtime dependencies, and SHALL advance the monotonic tick through a controlled timer-owned API rather than mutating tick state directly.

#### Scenario: Timer IRQ increments ticks

- **WHEN** i8259 IRQ0 fires after interrupts are enabled
- **THEN** the timer handler advances a monotonic kernel tick counter through a controlled timer-owned API such as `bigos::timer::on_tick()`
- **AND** the timer handler MUST NOT directly mutate timer-internal tick storage such as `bigos::timer::__detail::g_ticks`
- **AND** the handler returns through the existing external IRQ dispatch path

#### Scenario: Timer handler remains freestanding-safe

- **WHEN** the timer IRQ handler runs
- **THEN** it MUST NOT allocate memory, block, call filesystem code, depend on TTY, depend on scheduler services, access user mode state, or use hosted runtime APIs

#### Scenario: Timer handler does not own PIC EOI

- **WHEN** the timer IRQ handler completes
- **THEN** BigOS sends i8259 EOI through the existing external IRQ dispatch boundary
- **AND** the timer handler itself MUST NOT directly send i8259 EOI

### Requirement: Early tick and delay APIs have clear semantics

BigOS SHALL expose a minimal early-kernel timer API for reading timer ticks and performing coarse busy-wait delays under the current single-core early-kernel assumptions, with explicit context contracts for each API.

#### Scenario: Tick read returns a monotonic snapshot

- **WHEN** kernel code reads the timer tick API after timer initialization
- **THEN** BigOS returns a tick value that does not move backward within the current single-core early-kernel execution model

#### Scenario: Busy-wait delay is not scheduler sleep

- **WHEN** kernel code uses `mdelay()`, `sleep()`, or an equivalent delay primitive from this change
- **THEN** the primitive busy-waits rather than yielding to a scheduler or blocking queue
- **AND** the API documentation or comments state that it does not provide scheduler sleep, high precision, or SMP-safe timing semantics

#### Scenario: Delay primitive documents its execution-context precondition

- **WHEN** the busy-wait delay API is documented
- **THEN** the documentation states it must run in non-interrupt context with maskable interrupts enabled so that IRQ0 keeps advancing ticks
- **AND** the documentation states that calling it with interrupts disabled or inside an IRQ handler busy-waits forever
