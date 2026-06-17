## MODIFIED Requirements

### Requirement: Timer tick may record scheduler intent without violating IRQ contracts
BigOS SHALL allow the early timer runtime path to interact with the single-core scheduler through bounded IRQ-context-safe accounting, reschedule intent, and the documented capability IRQ-return scheduling while preserving timer tick ownership and the `on_tick()` context contract.

#### Scenario: Timer IRQ keeps tick ownership in timer API
- **WHEN** i8259 IRQ0 fires after the scheduler is introduced
- **THEN** the timer IRQ handler MUST continue to advance the monotonic tick through the timer-owned IRQ-context API such as `bigos::timer::on_tick()`
- **AND** scheduler integration MUST NOT require the IRQ layer to directly mutate timer-internal tick storage

#### Scenario: Timer scheduler hook is bounded
- **WHEN** timer IRQ0 notifies the scheduler about a tick or time slice
- **THEN** the scheduler-facing hook MUST be bounded and IRQ-context-safe
- **AND** it MUST NOT allocate memory, free memory, block, call `mdelay()`, perform console/serial bulk output, access filesystem services, or depend on user-mode services

#### Scenario: Reschedule intent is separated from ordinary allocation
- **WHEN** the timer tick marks that the current thread should yield at a later cooperative or preemptive scheduling boundary
- **THEN** the decision state MUST be represented without ordinary dynamic allocation in the IRQ handler
- **AND** the timer path MUST preserve the current single-core interrupt, EOI, and return-state contracts

#### Scenario: bounded timer-driven scheduler semantics may preempt on eligible IRQ return
- **WHEN** timer IRQ0 expires the current thread's time slice and the interrupt dispatch path reaches a documented safe return boundary
- **THEN** BigOS MAY perform a scheduler-owned context switch before returning to the interrupted kernel thread
- **AND** it MUST do so only when preemption is enabled, the current thread is eligible to be preempted, and the interrupt frame ABI and EOI ordering remain valid

## ADDED Requirements

### Requirement: Timer preemption accounting is deterministic
BigOS SHALL make timer-driven scheduler accounting deterministic enough for source-level checks and bounded runtime smoke validation.

#### Scenario: Time slice accounting uses monotonic tick
- **WHEN** scheduler preemption accounting is enabled
- **THEN** BigOS MUST base slice expiry on the existing monotonic PIT tick or an equivalent timer-owned tick event
- **AND** it MUST NOT introduce a separate unsynchronized timer source for scheduler slices

#### Scenario: Disabled preemption records pending intent
- **WHEN** timer IRQ0 observes slice expiry while preemption is disabled or the current context is not eligible for IRQ-return switching
- **THEN** BigOS MUST record pending reschedule intent or an equivalent bounded state
- **AND** it MUST NOT perform the context switch from inside the protected region

#### Scenario: Timer smoke markers remain bounded
- **WHEN** timer-driven preemption smoke emits validation markers
- **THEN** marker emission MUST be bounded and default-off through the relevant smoke configuration
- **AND** the normal timer smoke marker semantics MUST remain unchanged outside scheduler semantics validation
