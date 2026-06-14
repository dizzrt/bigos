## ADDED Requirements

### Requirement: Timer hardware and tick policy boundaries are separated

BigOS SHALL keep PIT hardware programming and i8259 IRQ delivery separate from timer-owned monotonic tick policy and scheduler-owned preemption decisions.

#### Scenario: PIT IRQ advances tick through timer owner

- **WHEN** PIT IRQ0 fires after interrupts are enabled
- **THEN** the IRQ handler MUST continue to advance monotonic time through a timer-owned IRQ-context-safe API
- **AND** the IRQ handler MUST NOT directly mutate timer-internal storage, perform scheduler queue manipulation that belongs to ordinary scheduler policy, or send i8259 EOI outside the interrupt dispatch boundary

#### Scenario: Hardware timer details remain driver side

- **WHEN** code configures PIT frequency, acknowledges hardware state, or documents PIT/i8259 assumptions
- **THEN** hardware-specific constants and port IO ordering MUST remain in driver or low-level IRQ code
- **AND** timer policy code MUST consume a semantic tick event rather than relying on raw PIT port programming details

### Requirement: Timer scheduler hook stays IRQ-context safe

BigOS timer-to-scheduler integration SHALL remain bounded, allocation-free, and safe for IRQ context.

#### Scenario: Scheduler hook records intent only

- **WHEN** timer tick processing determines that scheduler accounting or preemption intent should be updated
- **THEN** it MUST call a scheduler-owned bounded hook or update scheduler-owned bounded state
- **AND** it MUST NOT allocate memory, free memory, block, call `mdelay()`, perform bulk console/serial output, access filesystem services, or depend on user-mode services

#### Scenario: Timer API contracts remain explicit

- **WHEN** timer APIs are reviewed after boundary cleanup
- **THEN** each API used by IRQ, ordinary kernel thread, scheduler, or validation code MUST have an explicit context contract
- **AND** calls that require non-interrupt context MUST NOT be introduced into IRQ handlers or fatal exception paths

### Requirement: Timer boundary validation covers behavior and ownership

BigOS SHALL validate timer boundary changes with ownership checks and runtime checks appropriate to the modified path.

#### Scenario: Source checks cover owner boundaries

- **WHEN** timer IRQ or scheduler hook code changes
- **THEN** source-level checks or review notes MUST cover that tick ownership remains in the timer layer, EOI remains in interrupt dispatch, and scheduler intent remains bounded
- **AND** checks MUST distinguish source-only boundary changes from runtime behavior changes

#### Scenario: Runtime smoke covers IRQ0 when behavior changes

- **WHEN** runtime timer IRQ, PIT programming, scheduler preemption accounting, or IRQ-return behavior changes
- **THEN** validation MUST run the narrowest available build and, when available, QEMU headless timer/scheduler smoke
- **AND** if QEMU or Bochs validation cannot run, validation MUST record the missing dependency and remaining IRQ0/runtime risk
