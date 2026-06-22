## ADDED Requirements

### Requirement: External IRQ dispatch supports irqchip ownership
BigOS SHALL classify external IRQ vectors by their active irqchip owner before sending EOI. The dispatch layer MUST preserve CPU exception handling, syscall handling, ISR frame layout, and resumable interrupt return semantics while allowing supported external IRQs to be owned by either the documented PIC fallback path or the APIC-backed default path.

#### Scenario: APIC-owned IRQ avoids PIC EOI
- **WHEN** an external IRQ vector is classified as APIC-owned
- **THEN** BigOS MUST dispatch the registered handler through the existing interrupt frame ABI
- **AND** completion MUST use LAPIC EOI rather than i8259 EOI

#### Scenario: PIC-owned fallback IRQ avoids LAPIC EOI
- **WHEN** an external IRQ vector is classified as PIC-owned in the documented fallback path
- **THEN** BigOS MUST preserve the existing handler dispatch and PIC EOI behavior
- **AND** completion MUST NOT also use LAPIC EOI for that IRQ

#### Scenario: vector classification is deterministic
- **WHEN** an unsupported or unregistered external IRQ vector reaches the dispatch layer
- **THEN** BigOS MUST emit deterministic diagnostics containing the vector and ownership classification if known
- **AND** it MUST handle the vector without corrupting interrupt state or sending EOI through an unknown owner

### Requirement: APIC migration preserves interrupt ABI invariants
BigOS SHALL preserve the existing ISR ABI and interrupt-return invariants while migrating supported external IRQ sources to APIC-backed delivery.

#### Scenario: ISR frame layout remains stable
- **WHEN** an APIC-backed IRQ, local timer interrupt, or IPI enters the assembly ISR path
- **THEN** the C++ dispatch layer MUST receive the vector and `InterruptFrame` using the existing stable layout
- **AND** the return path MUST restore registers and return with `iretq` using the existing frame discipline

#### Scenario: syscall vector remains outside IRQ ownership
- **WHEN** vector `0x80` reaches the dispatch layer from user mode
- **THEN** BigOS MUST continue to handle it as the bounded syscall entry
- **AND** the APIC migration MUST NOT make the syscall path send PIC EOI, LAPIC EOI, or use external IRQ routing state

#### Scenario: CPU exception path remains separate
- **WHEN** vector `0x00` through `0x1f` reaches the dispatch layer
- **THEN** BigOS MUST continue to handle it as a CPU exception
- **AND** APIC-backed default interrupt delivery MUST NOT change exception error-code handling or page-fault recovery boundaries
