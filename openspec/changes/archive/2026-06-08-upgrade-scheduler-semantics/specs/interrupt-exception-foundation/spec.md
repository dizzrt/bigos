## MODIFIED Requirements

### Requirement: Scheduler integration preserves interrupt return semantics
BigOS SHALL integrate timer-driven kernel-thread scheduling with the interrupt runtime without changing the existing IDT ownership, exception/IRQ separation, i8259 EOI rules, syscall vector, or `InterruptFrame` ABI.

#### Scenario: IRQ dispatch still sends one EOI before return
- **WHEN** an external i8259 IRQ vector completes its registered handler after scheduler preemption integration exists
- **THEN** BigOS MUST still send exactly one i8259 EOI through the external IRQ dispatch boundary
- **AND** CPU exception vectors and the `int 0x80` syscall vector MUST still send no i8259 EOI

#### Scenario: IRQ return preserves saved frame semantics
- **WHEN** a scheduler decision is made from or after an IRQ path
- **THEN** the implementation MUST preserve the generated ISR frame layout, saved general-purpose register semantics, error-code slot semantics, and `iretq` return expectations
- **AND** any deferred reschedule or IRQ-return scheduling path MUST be covered by source-level checks or runtime validation notes

#### Scenario: Exception handlers do not become scheduling recovery paths
- **WHEN** CPU exception handlers such as the diagnostic-only `#PF` handler run
- **THEN** they MUST NOT attempt scheduler recovery, thread wakeup, demand paging, blocking waits, preemptive rescheduling, or retry of the faulting instruction
- **AND** fatal exception paths MUST continue to use deterministic diagnostic and halt behavior

#### Scenario: IRQ-return scheduling is guarded
- **WHEN** timer-driven reschedule intent exists at an external IRQ return boundary
- **THEN** BigOS MUST check that preemption is enabled, the interrupted context is eligible, and the current thread remains runnable before switching
- **AND** it MUST defer the switch when the interrupted context is inside a scheduler critical section, fatal diagnostic path, syscall dispatch forbidden region, or other documented non-preemptible region

#### Scenario: Syscall vector keeps early ABI boundary
- **WHEN** CPL3 code enters through `int 0x80` or kernel code handles the early syscall dispatch path
- **THEN** the syscall vector MUST preserve its existing ABI and EOI behavior
- **AND** the documented capability MUST NOT require syscall handlers to become sleepable, preemptible, or process-lifecycle aware

## ADDED Requirements

### Requirement: Interrupt frame preemption bridge is auditable
BigOS SHALL make the bridge between generated ISR frames and scheduler context switching explicit and auditable before enabling timer-driven IRQ-return preemption.

#### Scenario: Frame layout is reviewed before switch
- **WHEN** implementation adds or changes the IRQ-return scheduling bridge
- **THEN** validation MUST include source-level checks or review notes covering `InterruptFrame`, generated ISR stack layout, saved register order, error-code slot handling, and context-switch frame expectations
- **AND** the bridge MUST NOT silently reinterpret a CPU exception frame, syscall frame, or external IRQ frame as another frame type

#### Scenario: Switch path avoids ordinary allocator
- **WHEN** the IRQ-return scheduling bridge prepares or performs a context switch
- **THEN** it MUST NOT allocate or free ordinary memory, create scheduler objects, block, call `mdelay()`, access filesystem services, or depend on user-mode services
- **AND** any required thread/stack/scheduler ownership state MUST already exist before the IRQ path runs

#### Scenario: Low-level validation records residual risk
- **WHEN** timer preemption changes interrupt assembly, dispatch ordering, EOI ordering, or context-switch assembly
- **THEN** validation MUST record the executed source/build/runtime checks
- **AND** if Bochs or QEMU+Bochs cross-validation is unavailable, validation MUST record the missing dependency and residual hardware-behavior risk
