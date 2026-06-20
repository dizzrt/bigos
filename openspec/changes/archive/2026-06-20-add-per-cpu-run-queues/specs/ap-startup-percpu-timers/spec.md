## MODIFIED Requirements

### Requirement: per-CPU local timer 基线
BigOS SHALL initialize a local timer for each online CPU using PIT-referenced calibration for the first implementation and expose per-CPU tick state through the existing timer/scheduler context boundary. Before per-CPU run queues are enabled, AP ticks remain limited to CPU-local timer or idle validation; after per-CPU run queues are enabled, AP ticks MAY drive scheduling for work already assigned to that AP scheduler domain.

#### Scenario: BSP local timer 初始化
- **WHEN** the bootstrap CPU enables the APIC-backed timer baseline
- **THEN** BigOS MUST initialize a bounded local timer state for the bootstrap CPU
- **AND** it MUST calibrate or validate that timer against the PIT reference before publishing it as the runtime scheduler tick source

#### Scenario: PIT 校准失败
- **WHEN** the PIT-referenced LAPIC timer calibration fails or times out
- **THEN** BigOS MUST fail closed or continue in a documented BSP-only diagnostic path
- **AND** it MUST NOT publish an uncalibrated per-CPU timer as a valid scheduler tick source

#### Scenario: AP local timer 初始化
- **WHEN** an application processor reports online
- **THEN** BigOS MUST initialize that CPU's local timer state before allowing AP timer interrupts to be counted as valid
- **AND** AP local timer interrupts MUST update CPU-local tick state without entering unsupported filesystem, blocking I/O, or userland paths

#### Scenario: AP tick remains local before scheduler SMP
- **WHEN** an AP receives a local timer tick before per-CPU run queues and cross-CPU wakeups are implemented
- **THEN** BigOS MUST NOT migrate runnable work to or from that AP
- **AND** the tick MUST remain within the documented CPU-local timer or idle validation boundary

#### Scenario: AP tick drives local scheduling after scheduler SMP
- **WHEN** an AP receives a local timer tick after per-CPU run queues and cross-CPU wakeups are enabled
- **THEN** BigOS MAY account the AP's current ordinary thread and request preemption through that AP's scheduler domain
- **AND** it MUST NOT mutate the bootstrap CPU run queue, bootstrap current thread, or any other CPU's scheduler state except through documented cross-CPU scheduler boundaries
