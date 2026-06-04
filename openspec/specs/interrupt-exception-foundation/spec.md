## ADDED Requirements

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
