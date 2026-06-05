## ADDED Requirements

### Requirement: Scheduler integration preserves interrupt return semantics
BigOS SHALL integrate kernel-thread scheduling with the interrupt runtime without changing the existing IDT ownership, exception/IRQ separation, EOI rules, or `InterruptFrame` ABI.

#### Scenario: IRQ dispatch still sends one EOI before return
- **WHEN** an external i8259 IRQ vector completes its registered handler after scheduler integration exists
- **THEN** BigOS MUST still send exactly one i8259 EOI through the external IRQ dispatch boundary
- **AND** CPU exception vectors MUST still send no i8259 EOI

#### Scenario: IRQ return preserves saved frame semantics
- **WHEN** a scheduler decision is made from or after an IRQ path
- **THEN** the implementation MUST preserve the generated ISR frame layout, saved general-purpose register semantics, error-code slot semantics, and `iretq` return expectations
- **AND** any deferred reschedule or IRQ-return scheduling path MUST be covered by source-level checks or runtime validation notes

#### Scenario: Exception handlers do not become scheduling recovery paths
- **WHEN** CPU exception handlers such as the diagnostic-only `#PF` handler run
- **THEN** they MUST NOT attempt scheduler recovery, thread wakeup, demand paging, blocking waits, or retry of the faulting instruction
- **AND** fatal exception paths MUST continue to use deterministic diagnostic and halt behavior
