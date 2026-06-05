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
