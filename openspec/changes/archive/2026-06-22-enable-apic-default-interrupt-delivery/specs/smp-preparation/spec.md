## ADDED Requirements

### Requirement: APIC default interrupt delivery activation fulfills the prepared routing dependency
BigOS SHALL treat APIC-backed default interrupt delivery as an activated x86_64 SMP interrupt-routing capability once this change is implemented. Earlier SMP preparation boundaries that left complete APIC default external interrupt delivery as future work MUST now resolve to the bounded `apic-default-interrupt-delivery` capability while still excluding CPU hotplug, NUMA, MSI/MSI-X, complete IRQ affinity, and non-x86_64 backend parity.

#### Scenario: APIC default delivery no longer remains future-only
- **WHEN** BigOS boots with the APIC default delivery capability enabled and APIC initialization succeeds
- **THEN** supported default external IRQ sources MUST use the APIC-backed routing and EOI model
- **AND** implementation or validation notes MUST NOT describe complete APIC default delivery for those supported sources as future-only work

#### Scenario: unsupported interrupt features remain bounded
- **WHEN** implementation or validation references CPU hotplug, NUMA, MSI/MSI-X, broad device IRQ migration, complete IRQ affinity/load balancing, or backend parity outside x86_64
- **THEN** those references MUST remain non-goal or future-work statements
- **AND** they MUST NOT be claimed as delivered by APIC-backed default interrupt delivery

### Requirement: SMP interrupt paths preserve IRQ-safe locking classification
BigOS SHALL enforce the SMP preparation locking classification for APIC-backed external IRQs, per-CPU timer interrupts, scheduler nudge IPIs, TLB shootdown IPIs, and IRQ-return preemption.

#### Scenario: hard IRQ handlers remain non-blocking
- **WHEN** code runs from an APIC-backed external IRQ, local timer interrupt, scheduler-nudge IPI, TLB-shootdown IPI, or IRQ-return preemption boundary
- **THEN** it MUST NOT wait on scheduler-managed blocking primitives, perform filesystem or block I/O, or allocate through a path that can block
- **AND** it MUST use only synchronization documented as valid for hard IRQ context

#### Scenario: publication to interrupt targets is ordered
- **WHEN** ordinary kernel context publishes IOAPIC routing state, LAPIC timer state, IPI request state, or IRQ target CPU state for observation by an interrupt handler
- **THEN** the selected lock, interrupt-disable boundary, atomic operation, or architecture fence MUST provide the required visibility before the target CPU can act on that state
- **AND** validation MUST distinguish current-change diagnostics from historical freestanding/toolchain diagnostics

### Requirement: APIC default delivery has explicit fallback boundaries
BigOS SHALL keep fallback behavior explicit when APIC-backed default delivery is unavailable. Fallback MUST preserve the bounded single-CPU userland baseline or fail closed, and MUST NOT silently enable APs that depend on unavailable default interrupt delivery.

#### Scenario: BSP-only fallback is explicit
- **WHEN** APIC-backed default interrupt delivery is unavailable but the documented PIC/PIT fallback is selected
- **THEN** BigOS MUST restrict the runtime to the fallback's supported BSP-only interrupt model
- **AND** it MUST NOT route ordinary multi-core scheduling, IPI, or AP user execution through that fallback as if APIC default delivery were active

#### Scenario: unsafe fallback fails closed
- **WHEN** neither APIC-backed default delivery nor the documented PIC/PIT fallback can safely initialize required interrupt sources
- **THEN** BigOS MUST fail closed with deterministic diagnostics
- **AND** it MUST NOT continue into userland with unknown timer or external IRQ ownership
