## ADDED Requirements

### Requirement: Timer tick may record scheduler intent without violating IRQ contracts
BigOS SHALL allow the early timer runtime path to interact with the single-core scheduler only through bounded IRQ-context-safe accounting or reschedule intent, while preserving the existing timer tick ownership and `on_tick()` context contract. kernel thread scheduler capability SHALL NOT implement IRQ-return preemptive context switching.

#### Scenario: Timer IRQ keeps tick ownership in timer API
- **WHEN** i8259 IRQ0 fires after the scheduler is introduced
- **THEN** the timer IRQ handler MUST continue to advance the monotonic tick through the timer-owned IRQ-context API such as `bigos::timer::on_tick()`
- **AND** scheduler integration MUST NOT require the IRQ layer to directly mutate timer-internal tick storage

#### Scenario: Timer scheduler hook is bounded
- **WHEN** timer IRQ0 notifies the scheduler about a tick or time slice
- **THEN** the scheduler-facing hook MUST be bounded and IRQ-context-safe
- **AND** it MUST NOT allocate memory, free memory, block, call `mdelay()`, perform console/serial bulk output, access filesystem services, or depend on user-mode services

#### Scenario: Reschedule intent is separated from ordinary allocation
- **WHEN** the timer tick marks that the current thread should yield at a later cooperative or future scheduling boundary
- **THEN** the decision state MUST be represented without ordinary dynamic allocation in the IRQ handler
- **AND** the documented capability MUST preserve the current single-core interrupt, EOI, and return-state contracts by not switching threads on IRQ return
