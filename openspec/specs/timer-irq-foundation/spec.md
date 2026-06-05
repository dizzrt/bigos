## Purpose

Define the early x86_64 PIT and IRQ0 timer foundation for BigOS, including conservative PIT initialization, minimal tick handling, bounded smoke diagnostics, and validation expectations without introducing scheduler or hosted-runtime dependencies.

## Requirements

### Requirement: PIT initialization is explicit and conservative

BigOS SHALL initialize the legacy PIT/8253/8254 channel 0 as the first periodic timer source for the early x86_64 kernel, using explicit hardware constants and a named target frequency.

#### Scenario: PIT channel 0 is programmed before IRQ enable

- **WHEN** IRQ initialization prepares early external IRQ handlers
- **THEN** BigOS programs PIT channel 0 using data port `0x40` and command port `0x43` or documented equivalent constants
- **AND** BigOS derives the PIT divisor from the PIT base frequency and the configured target `TIMER_HZ` or equivalent constant
- **AND** this occurs before `irq::enableIRQ()` or any equivalent `sti` operation

#### Scenario: IRQ0 remains masked until handler registration

- **WHEN** the i8259 PIC has been initialized and all external IRQ lines are masked
- **THEN** BigOS keeps IRQ0 masked until the timer IRQ handler has been registered for vector `0x20`

### Requirement: Timer IRQ0 handler is minimal and observable

BigOS SHALL register an early timer handler for i8259 IRQ0 that proves periodic external IRQ delivery without introducing scheduler, blocking, allocation, or hosted runtime dependencies.

#### Scenario: Timer IRQ increments ticks

- **WHEN** i8259 IRQ0 fires after interrupts are enabled
- **THEN** the timer handler increments a monotonic kernel tick counter
- **AND** the handler returns through the existing external IRQ dispatch path

#### Scenario: Timer handler remains freestanding-safe

- **WHEN** the timer IRQ handler runs
- **THEN** it MUST NOT allocate memory, block, call filesystem code, depend on TTY, depend on scheduler services, access user mode state, or use hosted runtime APIs

#### Scenario: Timer handler does not own PIC EOI

- **WHEN** the timer IRQ handler completes
- **THEN** BigOS sends i8259 EOI through the existing external IRQ dispatch boundary
- **AND** the timer handler itself MUST NOT directly send i8259 EOI

### Requirement: Early tick and delay APIs have clear semantics

BigOS SHALL expose a minimal early-kernel timer API for reading timer ticks and performing coarse busy-wait delays under the current single-core early-kernel assumptions.

#### Scenario: Tick read returns a monotonic snapshot

- **WHEN** kernel code reads the timer tick API after timer initialization
- **THEN** BigOS returns a tick value that does not move backward within the current single-core early-kernel execution model

#### Scenario: Busy-wait delay is not scheduler sleep

- **WHEN** kernel code uses `mdelay()`, `sleep()`, or an equivalent delay primitive from this change
- **THEN** the primitive busy-waits rather than yielding to a scheduler or blocking queue
- **AND** the API documentation or comments state that it does not provide scheduler sleep, high precision, or SMP-safe timing semantics

### Requirement: Timer smoke is switchable and bounded

BigOS SHALL provide a validation-only timer smoke marker that is disabled in ordinary builds and bounded when enabled.

#### Scenario: Timer smoke is disabled by default

- **WHEN** BigOS is built without the timer smoke build switch
- **THEN** the timer IRQ path does not emit periodic `BIGOS_TIMER_IRQ` marker output during ordinary boot

#### Scenario: Timer smoke marker is observable when enabled

- **WHEN** BigOS is built with the timer smoke switch enabled and IRQ0 fires
- **THEN** BigOS emits a fixed `BIGOS_TIMER_IRQ` marker to an early observable output such as COM1 serial or VGA
- **AND** marker emission is limited to a bounded number of ticks or a documented one-shot condition

### Requirement: Timer initialization preserves existing boot and memory boundaries

BigOS SHALL integrate the timer without weakening the existing IRQ, exception, memory self-test, or address-layout invariants.

#### Scenario: Memory self-test remains IRQ-disabled

- **WHEN** `BIGOS_MM_SELF_TEST` or the equivalent memory runtime validation switch is enabled
- **THEN** BigOS runs the memory self-test before PIT initialization, timer IRQ0 unmask, PIC IRQ enable, and `irq::enableIRQ()`

#### Scenario: Exception and IRQ dispatch boundaries remain unchanged

- **WHEN** CPU exception vectors or external IRQ vectors reach the dispatch layer
- **THEN** CPU exception vectors still do not send PIC EOI
- **AND** external IRQ vectors, including timer vector `0x20`, still send EOI only after the registered handler or safe default handler completes

#### Scenario: Address layout remains stable

- **WHEN** timer support is added
- **THEN** BigOS does not move boot fixed addresses, linker higher-half base, kernel load base, BootInfo handoff ABI, recursive self-mapping, `KVMEM_BASE`, or the kernel direct-map region

### Requirement: Timer foundation validation is reproducible

BigOS SHALL validate timer foundation changes with source-level checks, cross-toolchain build, auxiliary diagnostics, and bounded emulator smoke when available.

#### Scenario: Source-level checks cover timer invariants

- **WHEN** this change is implemented
- **THEN** tests or static checks cover that timer IRQ0 is not unmasked before handler registration
- **AND** tests or static checks cover that timer handler code does not directly send PIC EOI
- **AND** tests or static checks cover that timer smoke output is build-switch gated and bounded

#### Scenario: Build validation covers timer sources

- **WHEN** C++ or assembly sources for timer, IRQ, PIC, or kernel initialization are changed
- **THEN** validation includes the narrowest useful `xmake`/cross-toolchain build
- **AND** clang or clangd auxiliary diagnostics are checked for modified C++ files when available

#### Scenario: Emulator smoke covers timer marker

- **WHEN** Bochs, ROM paths, generated disk image, serial marker oracle, and timer smoke switch are available
- **THEN** validation boots the kernel with timer smoke enabled and observes `BIGOS_TIMER_IRQ` within a bounded time

#### Scenario: Runtime smoke unavailable

- **WHEN** timer runtime smoke cannot run because the local emulator, ROM, cross toolchain, or serial oracle is unavailable
- **THEN** validation records the missing dependency, the alternative checks that passed, and the remaining bootability or IRQ0 risk
