## ADDED Requirements

### Requirement: Scheduler nudge is an IPI delivery consumer
BigOS SHALL route remote scheduler nudge delivery through the typed SMP IPI boundary when that boundary is active. Scheduler nudge MUST remain scoped to reschedule observation and MUST NOT imply TLB shootdown completion.

#### Scenario: remote enqueue uses scheduler-nudge IPI
- **WHEN** BigOS enqueues runnable work on a remote online CPU and that CPU may be idle or running a preemptible ordinary thread
- **THEN** it MUST publish the runnable state under the target scheduler domain protection before delivering a scheduler-nudge IPI
- **AND** the target CPU MUST observe the nudge through its CPU-local reschedule boundary

#### Scenario: scheduler nudge does not satisfy shootdown ordering
- **WHEN** a CPU receives or acknowledges a scheduler-nudge IPI
- **THEN** BigOS MUST NOT treat that acknowledgement as completion of any TLB shootdown request
- **AND** VM code that requires remote invalidation MUST use the TLB-shootdown IPI type and completion path

### Requirement: Scheduler and shootdown locks do not deadlock
BigOS SHALL define lock ordering between per-CPU scheduler domains, IPI request state, and TLB shootdown state so remote enqueue and VM invalidation cannot deadlock across CPUs.

#### Scenario: remote enqueue avoids shootdown wait locks
- **WHEN** scheduler code holds a per-CPU run queue lock while publishing remote runnable work
- **THEN** it MUST NOT wait for TLB shootdown completion while holding that run queue lock
- **AND** it MUST NOT require the remote IPI handler to acquire a lock held by the waiting CPU

#### Scenario: shootdown wait avoids scheduler blocking
- **WHEN** VM code waits for TLB-shootdown acknowledgements
- **THEN** it MUST NOT depend on scheduler-managed blocking wakeups from the target CPUs
- **AND** it MUST remain valid when target CPUs are in IRQ context or returning from an interrupt
