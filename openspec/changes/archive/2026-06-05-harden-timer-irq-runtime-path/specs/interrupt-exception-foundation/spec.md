## ADDED Requirements

### Requirement: ISR ABI runtime invariants are validated

BigOS SHALL validate the runtime ABI invariants of ISR entry into the C++ dispatch path without changing the existing `InterruptFrame` layout, register-save order, or exception-versus-IRQ EOI separation.

#### Scenario: Stack is 16-byte aligned before C++ dispatch

- **WHEN** an ISR stub builds the interrupt frame and calls the C++ dispatch entry
- **THEN** the stack is aligned to 16 bytes as required by the System V AMD64 calling convention before the call
- **AND** source-level or runtime checks cover this alignment invariant

#### Scenario: General-purpose registers are saved and restored in frame order

- **WHEN** an ISR stub saves and later restores general-purpose registers
- **THEN** the saved register state matches the `InterruptFrame` field order
- **AND** for an interrupt allowed to resume, the path restores that state and returns with `iretq`

#### Scenario: Error-code slot keeps frame layout stable

- **WHEN** a vector without a CPU-provided error code enters an ISR stub
- **THEN** the stub fills a synthetic zero error-code slot so the `InterruptFrame` layout stays identical to error-code vectors

#### Scenario: External IRQ returns after a single EOI

- **WHEN** an external IRQ vector including timer vector `0x20` completes its registered handler
- **THEN** BigOS sends exactly one i8259 EOI through the dispatch boundary
- **AND** the path returns with `iretq`
- **AND** CPU exception vectors still send no i8259 EOI

#### Scenario: ABI invariant validation is recorded

- **WHEN** ISR ABI invariants are validated
- **THEN** validation records the source-level checks that passed
- **AND** when a stable emulator oracle is available, validation records the bounded runtime observation confirming periodic IRQ delivery and correct return
- **AND** when runtime validation cannot run, validation records the missing dependency and remaining ABI risk
