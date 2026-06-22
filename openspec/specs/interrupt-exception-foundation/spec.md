## Purpose

Define the BigOS early x86_64 interrupt and exception foundation: explicit kernel-owned IDT setup, stable ISR dispatch ABI, exception-versus-IRQ separation, conservative i8259 PIC handling, bounded keyboard IRQ behavior, and reproducible validation of interrupt runtime invariants.

## Requirements

### Requirement: Kernel IDT initialization is explicit

BigOS SHALL initialize and load a kernel-owned static x86_64 IDT before enabling maskable interrupts, and SHALL use descriptor attributes appropriate for present ring-0 interrupt gates.

#### Scenario: Kernel loads IDT before enabling IRQ

- **WHEN** `kernel()` completes memory initialization and enters IRQ initialization
- **THEN** BigOS builds the kernel IDT entries for supported vectors
- **AND** BigOS executes an explicit kernel-stage `lidt` before any `sti` or equivalent IRQ enable operation

#### Scenario: IDT storage ownership is documented

- **WHEN** IDT storage is selected or changed
- **THEN** the implementation uses kernel-owned static storage for the kernel runtime IDT
- **AND** the implementation documents that the kernel runtime IDT no longer depends on legacy low-address `IDT_BASE = 0x1000` backing

### Requirement: ISR dispatch preserves interrupt context

BigOS SHALL route all generated ISR entries through a stable dispatch ABI that preserves enough interrupt context for C++ handlers to distinguish vectors, error codes, and return state.

#### Scenario: Exception with error code reaches dispatch

- **WHEN** a CPU exception that pushes an error code, such as `#PF`, enters an ISR stub
- **THEN** the C++ dispatch layer receives the original vector number and the CPU-provided error code without guessing stack layout

#### Scenario: Exception without error code reaches dispatch

- **WHEN** a CPU exception that does not push an error code enters an ISR stub
- **THEN** the C++ dispatch layer receives the original vector number and a synthetic zero error code

#### Scenario: Register save and restore are symmetric

- **WHEN** an ISR handler returns for an interrupt that is allowed to resume
- **THEN** the assembly path restores the saved register state and returns with `iretq` using the correct stack layout

### Requirement: CPU exceptions and external IRQs are separated

BigOS SHALL dispatch CPU exceptions separately from remapped i8259 external IRQs and MUST NOT send PIC EOI for CPU exception vectors.

#### Scenario: CPU exception does not send PIC EOI

- **WHEN** vector `0x00` through `0x1f` reaches the dispatch layer
- **THEN** BigOS handles it as a CPU exception
- **AND** BigOS MUST NOT issue i8259 EOI for that vector

#### Scenario: External IRQ sends PIC EOI after dispatch

- **WHEN** vector `0x20` through `0x2f` reaches the dispatch layer after i8259 remap
- **THEN** BigOS maps it to the corresponding i8259 IRQ line
- **AND** BigOS sends EOI after the registered handler or safe default IRQ handler completes

#### Scenario: Unknown vector is diagnosable

- **WHEN** an unsupported or unregistered vector reaches the dispatch layer
- **THEN** BigOS emits a deterministic diagnostic message containing the vector number
- **AND** BigOS handles the vector without corrupting interrupt state

### Requirement: External IRQ dispatch supports irqchip ownership
BigOS SHALL classify external IRQ vectors by their active irqchip owner before sending EOI. The dispatch layer MUST preserve CPU exception handling, syscall handling, ISR frame layout, and resumable interrupt return semantics while allowing supported external IRQs to be owned by either the documented PIC fallback path or the APIC-backed default path.

#### Scenario: APIC-owned IRQ avoids PIC EOI
- **WHEN** an external IRQ vector is classified as APIC-owned
- **THEN** BigOS MUST dispatch the registered handler through the existing interrupt frame ABI
- **AND** completion MUST use LAPIC EOI rather than i8259 EOI

#### Scenario: PIC-owned fallback IRQ avoids LAPIC EOI
- **WHEN** an external IRQ vector is classified as PIC-owned in the documented fallback path
- **THEN** BigOS MUST preserve the existing handler dispatch and PIC EOI behavior
- **AND** completion MUST NOT also use LAPIC EOI for that IRQ

#### Scenario: vector classification is deterministic
- **WHEN** an unsupported or unregistered external IRQ vector reaches the dispatch layer
- **THEN** BigOS MUST emit deterministic diagnostics containing the vector and ownership classification if known
- **AND** it MUST handle the vector without corrupting interrupt state or sending EOI through an unknown owner

### Requirement: APIC migration preserves interrupt ABI invariants
BigOS SHALL preserve the existing ISR ABI and interrupt-return invariants while migrating supported external IRQ sources to APIC-backed delivery.

#### Scenario: ISR frame layout remains stable
- **WHEN** an APIC-backed IRQ, local timer interrupt, or IPI enters the assembly ISR path
- **THEN** the C++ dispatch layer MUST receive the vector and `InterruptFrame` using the existing stable layout
- **AND** the return path MUST restore registers and return with `iretq` using the existing frame discipline

#### Scenario: syscall vector remains outside IRQ ownership
- **WHEN** vector `0x80` reaches the dispatch layer from user mode
- **THEN** BigOS MUST continue to handle it as the bounded syscall entry
- **AND** the APIC migration MUST NOT make the syscall path send PIC EOI, LAPIC EOI, or use external IRQ routing state

#### Scenario: CPU exception path remains separate
- **WHEN** vector `0x00` through `0x1f` reaches the dispatch layer
- **THEN** BigOS MUST continue to handle it as a CPU exception
- **AND** APIC-backed default interrupt delivery MUST NOT change exception error-code handling or page-fault recovery boundaries

### Requirement: Page fault handler is diagnostic-only

BigOS SHALL provide an early `#PF` handler that reports page fault diagnostics and halts safely, without attempting page fault recovery.

#### Scenario: Page fault diagnostic includes fault address

- **WHEN** vector `0x0e` reaches the exception dispatch layer
- **THEN** BigOS reads `CR2`
- **AND** BigOS emits the dedicated fixed `BIGOS_PAGE_FAULT` marker containing the fault address and error code

#### Scenario: Page fault error code is decoded

- **WHEN** the `#PF` handler reports the error code
- **THEN** the diagnostic output identifies at least present, write, user, reserved-bit, and instruction-fetch meanings when those bits are available

#### Scenario: Page fault does not recover

- **WHEN** the `#PF` handler finishes emitting diagnostics
- **THEN** BigOS halts safely
- **AND** BigOS MUST NOT allocate pages, modify page tables, retry the faulting instruction, or claim demand paging support

### Requirement: i8259 PIC initialization is ordered and conservative

BigOS SHALL initialize the i8259 PIC with the existing remap base and keep external IRQ lines masked until handlers are installed.

#### Scenario: PIC remap occurs before unmask

- **WHEN** IRQ initialization runs
- **THEN** BigOS initializes the master PIC at vector base `0x20` and the slave PIC at vector base `0x28`
- **AND** all external IRQ lines remain masked until the corresponding handler is ready

#### Scenario: Keyboard IRQ unmask requires handler

- **WHEN** BigOS unmasks i8259 IRQ1
- **THEN** a keyboard IRQ handler has already been registered for vector `0x21`

#### Scenario: IRQ enable follows PIC readiness

- **WHEN** `kernel()` enables maskable interrupts
- **THEN** IDT initialization, exception registration, PIC initialization, and selected IRQ handler registration have completed

### Requirement: Keyboard IRQ1 input handoff preserves interrupt boundaries

BigOS SHALL allow keyboard IRQ1 to hand input to the TTY layer only after preserving the existing interrupt dispatch boundaries: handler registration precedes unmask, the handler does not send EOI, and external IRQ dispatch sends EOI after the handler returns.

#### Scenario: Keyboard IRQ1 unmask waits for input readiness

- **WHEN** BigOS unmasks i8259 IRQ1 for keyboard input rather than smoke-only validation
- **THEN** a keyboard handler has already been registered for vector `0x21`
- **AND** the TTY input buffer and keyboard decoder state have been initialized

#### Scenario: Keyboard handler returns to dispatch for EOI

- **WHEN** keyboard IRQ1 reaches the registered handler
- **THEN** the handler performs bounded input handoff and returns
- **AND** the handler MUST NOT directly send i8259 EOI
- **AND** the external IRQ dispatch path sends the single required EOI after handler completion

#### Scenario: Keyboard IRQ1 does not depend on scheduler or user mode

- **WHEN** keyboard IRQ1 is enabled during the early kernel runtime
- **THEN** the input handoff path remains valid without scheduler, blocking waits, processes, syscalls, user-mode address spaces, filesystem services, or SMP support

### Requirement: Keyboard IRQ smoke is minimal and observable

BigOS SHALL provide a minimal keyboard IRQ1 smoke path that proves external IRQ delivery and handler dispatch without introducing a full input subsystem.

#### Scenario: Keyboard IRQ reads scancode

- **WHEN** keyboard IRQ1 fires
- **THEN** the handler reads one scancode byte from PS/2 data port `0x60`
- **AND** the handler emits an observable marker or temporary output containing the scancode

#### Scenario: Keyboard IRQ remains freestanding-safe

- **WHEN** the keyboard IRQ handler runs
- **THEN** it MUST NOT require heap allocation, scheduler services, blocking waits, filesystem access, hosted runtime APIs, or a complete TTY layer

#### Scenario: Keyboard smoke validation is recorded

- **WHEN** manual Bochs keyboard input or existing emulator input capability is available
- **THEN** validation records that IRQ1 reached the handler and produced the expected observable marker
- **AND** if keyboard input cannot be exercised, validation records the missing dependency and remaining risk

### Requirement: Interrupt foundation validation is reproducible

BigOS SHALL validate interrupt and exception foundation changes with source-level checks, cross-toolchain build, diagnostics, and bounded emulator smoke when available.

#### Scenario: Source-level checks cover interrupt invariants

- **WHEN** this change is implemented
- **THEN** tests or static checks cover that exception vectors do not send PIC EOI
- **AND** tests or static checks cover that keyboard IRQ1 is not unmasked before handler registration

#### Scenario: Build validation covers low-level sources

- **WHEN** C++ or assembly sources for IRQ, PIC, keyboard, or kernel initialization are changed
- **THEN** validation includes the narrowest useful `xmake`/cross-toolchain build
- **AND** IDE diagnostics or clang/clangd auxiliary diagnostics are checked for modified C++ files when available

#### Scenario: Emulator smoke covers normal boot

- **WHEN** Bochs, ROM paths, generated disk image, and smoke oracle are available
- **THEN** validation boots the kernel with interrupts enabled and observes the normal boot marker within a bounded time

#### Scenario: Emulator smoke covers page fault diagnostic

- **WHEN** a validation-only page fault trigger is enabled and Bochs smoke is available
- **THEN** validation observes the fixed `BIGOS_PAGE_FAULT` marker and confirms the kernel halts safely

### Requirement: ISR ABI runtime invariants are validated

BigOS SHALL validate the runtime ABI invariants of ISR entry into the C++ dispatch path without changing the existing `InterruptFrame` layout, register-save order, or exception-versus-IRQ EOI separation.

#### Scenario: Stack is 16-byte aligned before C++ dispatch

- **WHEN** an ISR stub builds the interrupt frame and calls the C++ dispatch entry
- **THEN** the stack is aligned to 16 bytes as required by the System V AMD64 calling convention before the call
- **AND** source-level or runtime checks cover this alignment invariant

#### Scenario: General-purpose registers are saved and restored in frame order

- **WHEN** an ISR stub saves and later restores general-purpose registers
- **THEN** the saved register state matches the `InterruptFrame` field order
- **AND** for an interrupt allowed to resume, the path restores that state and returns with `iretq`

#### Scenario: Error-code slot keeps frame layout stable

- **WHEN** a vector without a CPU-provided error code enters an ISR stub
- **THEN** the stub fills a synthetic zero error-code slot so the `InterruptFrame` layout stays identical to error-code vectors

#### Scenario: External IRQ returns after a single EOI

- **WHEN** an external IRQ vector including timer vector `0x20` completes its registered handler
- **THEN** BigOS sends exactly one i8259 EOI through the dispatch boundary
- **AND** the path returns with `iretq`
- **AND** CPU exception vectors still send no i8259 EOI

#### Scenario: ABI invariant validation is recorded

- **WHEN** ISR ABI invariants are validated
- **THEN** validation records the source-level checks that passed
- **AND** when a stable emulator oracle is available, validation records the bounded runtime observation confirming periodic IRQ delivery and correct return
- **AND** when runtime validation cannot run, validation records the missing dependency and remaining ABI risk

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
- **AND** bounded timer-driven scheduler semantics MUST NOT require syscall handlers to become sleepable, preemptible, or process-lifecycle aware

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

### Requirement: Interrupt dispatch exposes architecture boundary semantics

BigOS interrupt dispatch SHALL preserve the existing exception, external IRQ, and syscall entry semantics while exposing a clear boundary for core policy to consume interrupt results without owning x86_64 entry details.

#### Scenario: Core code consumes dispatch outcome not raw frame layout

- **WHEN** core timer or scheduler code is reached from an external IRQ path
- **THEN** it MUST consume a documented semantic hook or dispatch result rather than open-coding x86_64 ISR stack offsets, IDT descriptor details, or raw PIC EOI sequencing
- **AND** x86_64-specific frame construction and return mechanics MUST remain owned by the low-level interrupt entry/exit implementation

#### Scenario: EOI boundary remains single owner

- **WHEN** an external i8259 IRQ vector completes its registered handler and any bounded scheduler-facing notification
- **THEN** BigOS MUST still send exactly one i8259 EOI through the external IRQ dispatch boundary
- **AND** CPU exception vectors and the `int 0x80` syscall vector MUST still send no i8259 EOI

### Requirement: Interrupt boundary cleanup preserves diagnostic paths

BigOS SHALL keep fatal exception and unsupported interrupt behavior deterministic while boundary cleanup is performed.

#### Scenario: Exception path is not a scheduler recovery path

- **WHEN** CPU exception handlers such as unsupported kernel faults or diagnostic page faults run
- **THEN** they MUST NOT become scheduler recovery, timer wakeup, blocking wait, demand-paging retry, or preemptive reschedule paths unless a separate capability explicitly introduces that behavior
- **AND** existing diagnostic or panic behavior MUST remain deterministic

#### Scenario: Unknown vector remains diagnosable

- **WHEN** an unsupported or unregistered vector reaches dispatch after boundary cleanup
- **THEN** BigOS MUST emit deterministic diagnostic information for that vector or follow the existing safe default handler
- **AND** the handler MUST NOT corrupt saved interrupt state or bypass the documented EOI rules

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
