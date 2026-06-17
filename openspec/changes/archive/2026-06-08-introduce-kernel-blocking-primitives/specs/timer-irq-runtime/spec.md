## ADDED Requirements

### Requirement: Timer supports cooperative sleep and timeout waits
BigOS SHALL provide timer-backed sleep or timeout wait primitives that use the monotonic tick to wake blocked threads under the single-core cooperative scheduler model.

#### Scenario: Sleep wait records deadline
- **WHEN** ordinary non-interrupt kernel code requests a sleep or timeout wait for a bounded tick duration
- **THEN** BigOS MUST record a deadline based on the existing monotonic tick
- **AND** the current thread MUST become non-runnable until the deadline expires or another wakeup/cancellation transition occurs

#### Scenario: Expired sleeper is woken
- **WHEN** timer processing observes that a sleeping thread's deadline has expired
- **THEN** BigOS MUST transition that thread to runnable with a deterministic timeout result
- **AND** the transition MUST NOT require ordinary allocator APIs in IRQ context

### Requirement: Timer IRQ integration remains bounded
BigOS SHALL keep timer IRQ0 handling bounded and IRQ-context safe while supporting timeout waits.

#### Scenario: on_tick keeps IRQ-safe contract
- **WHEN** PIT IRQ0 fires after timeout wait support exists
- **THEN** the IRQ path MUST continue to advance the monotonic tick through the timer-owned API such as `bigos::timer::on_tick()`
- **AND** `on_tick()` and any timer IRQ scheduler hook MUST NOT allocate memory, free memory, block, call `mdelay()`, access filesystem services, depend on user-mode services, or perform bulk console/serial output

#### Scenario: Timer IRQ does not preemptively switch
- **WHEN** a timeout expires during timer IRQ0
- **THEN** the IRQ path MAY mark or wake the expired thread through a bounded IRQ-safe path
- **AND** it MUST NOT perform a full context switch on IRQ return in the documented capability
- **AND** external IRQ EOI and `iretq` return semantics MUST remain unchanged
