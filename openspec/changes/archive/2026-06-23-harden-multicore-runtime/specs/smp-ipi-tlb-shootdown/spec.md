## ADDED Requirements

### Requirement: IPI delivery hardening is stress validated
BigOS SHALL validate typed IPI delivery under bounded multi-core stress, including scheduler-nudge and TLB-shootdown vectors, target CPU filtering, acknowledgement ownership, and failure diagnostics.

#### Scenario: scheduler and shootdown IPIs remain distinct under stress
- **WHEN** scheduler-nudge and TLB-shootdown IPIs are delivered concurrently or in rapid sequence
- **THEN** BigOS MUST keep their vector classification, acknowledgement state, and completion semantics distinct
- **AND** scheduler-nudge handling MUST NOT satisfy TLB-shootdown completion

#### Scenario: IPI target filtering rejects invalid CPUs
- **WHEN** IPI stress attempts to deliver to an offline, failed, unsupported, or non-schedulable CPU
- **THEN** BigOS MUST reject or exclude the target through the typed IPI boundary
- **AND** it MUST NOT wait indefinitely for an acknowledgement from that CPU

### Requirement: TLB shootdown hardening prevents stale translation reuse
BigOS SHALL fail closed when required remote TLB shootdown completion cannot be proven, and SHALL prevent frame reclamation, address reuse, or user-mode return that depends on missing invalidation.

#### Scenario: shootdown completion waits for required targets
- **WHEN** a page-table change invalidates mappings for an `mm context` that may be resident on remote online CPUs
- **THEN** BigOS MUST publish the page-table update before delivering shootdown IPIs
- **AND** it MUST wait for every required target acknowledgement before releasing frames or reporting the operation complete

#### Scenario: shootdown timeout fails closed with diagnostics
- **WHEN** a required remote CPU does not acknowledge TLB shootdown before the bounded timeout
- **THEN** BigOS MUST emit diagnostics that identify the requesting CPU, target set, missing acknowledgement, affected `mm context`, and shootdown generation or equivalent token
- **AND** it MUST enter a deterministic fail-closed path

### Requirement: IPI and shootdown handlers remain allocation-free under hardening
BigOS SHALL keep scheduler-nudge and TLB-shootdown handlers IRQ-safe and allocation-free even when hardening diagnostics or stress validation is enabled.

#### Scenario: handler executes in hard IRQ context
- **WHEN** a CPU receives a scheduler-nudge or TLB-shootdown IPI during hardening validation
- **THEN** the handler MUST only update CPU-local, IRQ-safe, or preallocated acknowledgement state
- **AND** it MUST NOT allocate ordinary dynamic memory, enter filesystem/block I/O, fault in user memory, or block on scheduler primitives

#### Scenario: diagnostics use preallocated or panic-safe state
- **WHEN** an IPI or shootdown timeout diagnostic is emitted from a failure path
- **THEN** BigOS MUST use panic-safe serial/VGA diagnostic boundaries and preexisting state
- **AND** it MUST NOT require successful dynamic allocation to report the failure
