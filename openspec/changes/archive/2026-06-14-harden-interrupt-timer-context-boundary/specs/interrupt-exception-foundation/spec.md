## ADDED Requirements

### Requirement: Interrupt dispatch exposes architecture boundary semantics

BigOS interrupt dispatch SHALL preserve the existing exception, external IRQ, and syscall entry semantics while exposing a clear boundary for core policy to consume interrupt results without owning x86_64 entry details.

#### Scenario: Core code consumes dispatch outcome not raw frame layout

- **WHEN** core timer or scheduler code is reached from an external IRQ path
- **THEN** it MUST consume a documented semantic hook or dispatch result rather than open-coding x86_64 ISR stack offsets, IDT descriptor details, or raw PIC EOI sequencing
- **AND** x86_64-specific frame construction and return mechanics MUST remain owned by the low-level interrupt entry/exit implementation

#### Scenario: EOI boundary remains single owner

- **WHEN** an external i8259 IRQ vector completes its registered handler and any bounded scheduler-facing notification
- **THEN** BigOS MUST still send exactly one i8259 EOI through the external IRQ dispatch boundary
- **AND** CPU exception vectors and the `int 0x80` syscall vector MUST still send no i8259 EOI

### Requirement: Interrupt boundary cleanup preserves diagnostic paths

BigOS SHALL keep fatal exception and unsupported interrupt behavior deterministic while boundary cleanup is performed.

#### Scenario: Exception path is not a scheduler recovery path

- **WHEN** CPU exception handlers such as unsupported kernel faults or diagnostic page faults run
- **THEN** they MUST NOT become scheduler recovery, timer wakeup, blocking wait, demand-paging retry, or preemptive reschedule paths unless a separate capability explicitly introduces that behavior
- **AND** existing diagnostic or panic behavior MUST remain deterministic

#### Scenario: Unknown vector remains diagnosable

- **WHEN** an unsupported or unregistered vector reaches dispatch after boundary cleanup
- **THEN** BigOS MUST emit deterministic diagnostic information for that vector or follow the existing safe default handler
- **AND** the handler MUST NOT corrupt saved interrupt state or bypass the documented EOI rules
