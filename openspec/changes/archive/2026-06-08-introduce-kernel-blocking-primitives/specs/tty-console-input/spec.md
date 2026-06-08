## ADDED Requirements

### Requirement: TTY provides non-interrupt blocking input wait
BigOS SHALL add a blocking TTY input consumer path for ordinary non-interrupt kernel threads while preserving the existing IRQ-safe producer and non-blocking read behavior.

#### Scenario: Empty TTY read may block in thread context
- **WHEN** ordinary non-interrupt kernel code requests a blocking TTY input read and the input buffer is empty
- **THEN** BigOS MUST put the current thread on a TTY input wait queue or equivalent wait object
- **AND** the thread MUST become non-runnable until input arrives, timeout expires, or the wait is cancelled

#### Scenario: Existing non-blocking read remains available
- **WHEN** kernel code uses the existing non-blocking TTY poll or drain API
- **THEN** an empty input buffer MUST still return a non-blocking empty result
- **AND** this behavior MUST NOT depend on scheduler progress or blocking primitives

#### Scenario: Keyboard IRQ wakes TTY waiter safely
- **WHEN** keyboard IRQ1 enqueues translated input into the TTY input buffer and one or more threads wait for input
- **THEN** BigOS MAY wake a waiting TTY consumer through a bounded IRQ-safe wakeup path
- **AND** the keyboard IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, depend on user-mode services, or directly perform a context switch

### Requirement: TTY blocking validation is deterministic
BigOS SHALL validate blocking TTY input behavior without requiring an interactive keyboard path for automated smoke.

#### Scenario: Synthetic producer wakes blocking reader
- **WHEN** TTY blocking smoke is enabled
- **THEN** validation MUST use a deterministic producer, timeout, or documented manual input path to wake a blocked reader
- **AND** the smoke MUST emit fixed `BIGOS_` markers that identify the reader blocked, input arrived or timeout expired, and the reader resumed

#### Scenario: Manual keyboard validation is optional
- **WHEN** emulator keyboard injection or manual input is unavailable
- **THEN** validation MUST record the missing input capability and the source/build checks that still cover IRQ-safe producer boundaries
