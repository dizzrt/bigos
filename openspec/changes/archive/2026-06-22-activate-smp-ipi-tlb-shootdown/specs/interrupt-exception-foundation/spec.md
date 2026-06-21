## ADDED Requirements

### Requirement: LAPIC IPI vectors are separated from exceptions, legacy IRQs, and syscalls
BigOS SHALL classify SMP IPI vectors as LAPIC-owned internal interrupt vectors distinct from CPU exceptions, remapped i8259 external IRQs, and the software syscall vector. IPI dispatch MUST use LAPIC EOI ownership and MUST NOT send i8259 EOI or enter the syscall path.

#### Scenario: IPI vector uses LAPIC EOI
- **WHEN** a scheduler-nudge or TLB-shootdown IPI reaches the interrupt dispatch layer
- **THEN** BigOS MUST route it to the registered IPI handler for that vector
- **AND** completion MUST send EOI through the LAPIC-owned boundary, not through the i8259 PIC path

#### Scenario: syscall vector remains separate
- **WHEN** vector `0x80` reaches the dispatch layer from user mode
- **THEN** BigOS MUST continue to handle it as the bounded syscall entry
- **AND** it MUST NOT send LAPIC IPI EOI or i8259 EOI for the syscall path

#### Scenario: legacy IRQ behavior remains stable
- **WHEN** remapped i8259 IRQ vectors reach the dispatch layer
- **THEN** BigOS MUST preserve the existing external IRQ dispatch and single i8259 EOI behavior
- **AND** the presence of SMP IPI vectors MUST NOT change keyboard, PIT fallback, or other legacy IRQ classification

### Requirement: IPI dispatch preserves ISR ABI and IRQ-return semantics
BigOS SHALL deliver IPI handlers through the existing ISR frame discipline without changing `InterruptFrame` layout, register-save order, exception error-code handling, or `iretq` return semantics.

#### Scenario: IPI handler receives stable interrupt context
- **WHEN** an IPI vector enters the assembly ISR path
- **THEN** the C++ dispatch layer MUST receive the vector and interrupt frame using the same stable ABI as other resumable interrupts
- **AND** the handler MUST return through the normal register restore and `iretq` path

#### Scenario: IRQ-return preemption remains CPU-local
- **WHEN** an IPI handler sets CPU-local reschedule state
- **THEN** IRQ-return preemption MUST consult only that CPU's scheduler domain
- **AND** it MUST NOT mutate another CPU's run queue or BSP-only scheduler state outside documented cross-CPU scheduler boundaries
