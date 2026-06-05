## ADDED Requirements

### Requirement: Keyboard IRQ1 input handoff preserves interrupt boundaries

BigOS SHALL allow keyboard IRQ1 to hand input to the TTY layer only after preserving the existing interrupt dispatch boundaries: handler registration precedes unmask, the handler does not send EOI, and external IRQ dispatch sends EOI after the handler returns.

#### Scenario: Keyboard IRQ1 unmask waits for input readiness

- **WHEN** BigOS unmasks i8259 IRQ1 for keyboard input rather than smoke-only validation
- **THEN** a keyboard handler has already been registered for vector `0x21`
- **AND** the TTY input buffer and keyboard decoder state have been initialized

#### Scenario: Keyboard handler returns to dispatch for EOI

- **WHEN** keyboard IRQ1 reaches the registered handler
- **THEN** the handler performs bounded input handoff and returns
- **AND** the handler MUST NOT directly send i8259 EOI
- **AND** the external IRQ dispatch path sends the single required EOI after handler completion

#### Scenario: Keyboard IRQ1 does not depend on scheduler or user mode

- **WHEN** keyboard IRQ1 is enabled during the early kernel runtime
- **THEN** the input handoff path remains valid without scheduler, blocking waits, processes, syscalls, user-mode address spaces, filesystem services, or SMP support
