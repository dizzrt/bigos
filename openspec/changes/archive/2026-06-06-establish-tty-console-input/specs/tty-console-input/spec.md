## ADDED Requirements

### Requirement: Keyboard input is decoded into bounded TTY events

BigOS SHALL convert supported PS/2 set-1 keyboard scancodes into bounded TTY input events or ASCII characters without requiring scheduler, heap allocation, filesystem, user mode, or hosted runtime services.

#### Scenario: Supported scancode becomes ASCII input

- **WHEN** keyboard IRQ1 delivers a supported PS/2 set-1 make scancode for a printable US-layout key
- **THEN** BigOS converts it to the corresponding ASCII character using the current modifier state
- **AND** BigOS enqueues the character into the TTY input buffer

#### Scenario: Modifier state affects key translation

- **WHEN** Shift make and break scancodes are observed around an alphabetic or symbol key
- **THEN** BigOS updates modifier state and applies it to subsequent supported key translations
- **AND** modifier state updates MUST NOT enqueue printable input by themselves

#### Scenario: Unsupported scancode is safe

- **WHEN** keyboard IRQ1 delivers an unsupported, extended, or unmapped scancode
- **THEN** BigOS drops or records the scancode without corrupting input buffer state
- **AND** BigOS MUST NOT panic, allocate memory, block, or call hosted runtime APIs

### Requirement: Keyboard ISR is IRQ-context safe

BigOS SHALL keep the keyboard IRQ1 handler bounded and IRQ-context safe while handing input to the TTY layer.

#### Scenario: ISR performs bounded input handoff

- **WHEN** the keyboard IRQ1 handler runs
- **THEN** it reads one byte from PS/2 data port `0x60`
- **AND** it performs only bounded modifier/key translation and fixed-capacity input enqueue operations
- **AND** it returns to the IRQ dispatch layer for EOI

#### Scenario: ISR avoids complex runtime dependencies

- **WHEN** the keyboard IRQ1 handler runs
- **THEN** it MUST NOT allocate or free memory
- **AND** it MUST NOT block, sleep, poll for line input, call `mdelay()`, use filesystem services, call `kprintf`, or depend on scheduler, process, syscall, user-mode, or TTY consumer progress

#### Scenario: ISR does not directly write console output

- **WHEN** keyboard input is received in IRQ context
- **THEN** the ISR MUST NOT directly write VGA text output or serial formatted output for normal input handling
- **AND** any echo or smoke marker is emitted from a non-interrupt consumer path or from a separately bounded validation path

### Requirement: TTY input buffer is fixed-capacity and non-blocking

BigOS SHALL provide a fixed-capacity input buffer that supports IRQ-context producers and non-interrupt consumers under the current single-core early-kernel model.

#### Scenario: IRQ producer enqueues without allocation

- **WHEN** translated keyboard input is available in IRQ context
- **THEN** BigOS enqueues it into statically owned or initialization-owned fixed-capacity storage
- **AND** enqueue MUST NOT allocate memory or wait for space

#### Scenario: Full buffer is handled deterministically

- **WHEN** translated input arrives and the TTY input buffer is full
- **THEN** BigOS drops the new input or records an overflow counter deterministically
- **AND** BigOS MUST NOT overwrite unread input unless the overflow policy is explicitly documented

#### Scenario: Non-interrupt consumer reads available input

- **WHEN** non-interrupt kernel code polls or drains the TTY input buffer
- **THEN** BigOS returns available characters in FIFO order
- **AND** an empty buffer returns a non-blocking empty result rather than sleeping

### Requirement: Console output has a unified minimal API

BigOS SHALL expose a minimal console/TTY output API for ordinary kernel text output while preserving existing early diagnostic direct-output paths.

#### Scenario: Console writes visible text

- **WHEN** kernel code writes a character or string through the console/TTY output API after initialization
- **THEN** BigOS emits the text to the VGA text-mode backend
- **AND** BigOS does not mirror ordinary console text to COM1 serial by default
- **AND** BigOS keeps output behavior freestanding-safe without requiring heap allocation or hosted formatting

#### Scenario: Basic control characters are handled

- **WHEN** console output receives newline, carriage return, tab, or backspace
- **THEN** BigOS applies a documented minimal text-mode behavior for that control character
- **AND** unsupported terminal escape sequences are ignored or emitted literally according to the documented policy

#### Scenario: Early diagnostics remain independent

- **WHEN** early panic, page fault diagnostics, memory self-test markers, or other fatal paths emit deterministic markers
- **THEN** those paths MAY continue using direct VGA/COM1 output APIs
- **AND** they MUST NOT depend on TTY initialization or input buffer state

#### Scenario: Existing direct output remains early-only

- **WHEN** this change introduces the console/TTY output API
- **THEN** existing `kput()` and `kputs()` direct-output APIs retain their early diagnostic semantics
- **AND** ordinary runtime console output uses the new console API rather than changing `kput()` or `kputs()` into console wrappers

### Requirement: TTY and keyboard initialization order is safe

BigOS SHALL initialize keyboard input, TTY buffers, console output, and IRQ unmasking in an order that prevents IRQ1 from reaching an unready input path.

#### Scenario: TTY state exists before keyboard IRQ1 unmask

- **WHEN** BigOS unmasks i8259 IRQ1 for keyboard input or keyboard smoke
- **THEN** the keyboard handler has been registered
- **AND** the TTY input buffer and any required keyboard state have been initialized

#### Scenario: Kernel enables interrupts after selected input readiness

- **WHEN** `kernel()` executes `sti` or equivalent maskable interrupt enable
- **THEN** IDT setup, PIC remap, selected ISR registration, timer readiness, and selected keyboard/TTY readiness have completed

#### Scenario: Default boot remains conservative

- **WHEN** keyboard input is not selected by the default boot policy or validation switch
- **THEN** BigOS keeps keyboard IRQ1 masked
- **AND** the absence of keyboard input MUST NOT prevent timer IRQ0, normal boot marker, memory self-test, or diagnostic exception paths from working

### Requirement: Keyboard and TTY validation is reproducible

BigOS SHALL validate the keyboard/TTY/console change with source-level checks, cross-toolchain builds, and bounded emulator smoke when available.

#### Scenario: Source checks cover IRQ safety

- **WHEN** this change is implemented
- **THEN** tests or static checks confirm keyboard IRQ1 handler registration precedes IRQ1 unmask
- **AND** tests or static checks confirm the keyboard ISR does not directly call `kprintf`, `kput`, allocation APIs, blocking waits, or `mdelay()`

#### Scenario: Source checks cover input behavior

- **WHEN** this change is implemented
- **THEN** tests or static checks cover representative set-1 scancode to ASCII mappings
- **AND** tests or static checks cover modifier state, input buffer FIFO behavior, empty reads, and full-buffer overflow policy

#### Scenario: Build validation covers default and keyboard modes

- **WHEN** keyboard/TTY/console sources are changed
- **THEN** validation includes the narrowest useful `xmake` or cross-toolchain build for default configuration
- **AND** validation includes a build with keyboard input or `keyboard_smoke` enabled when that switch exists

#### Scenario: Manual runtime smoke is recorded

- **WHEN** Bochs, ROM paths, disk image generation, VGA/serial oracle, and manual keyboard input are available
- **THEN** validation records bounded evidence that keyboard input reaches the TTY path and visible console output or marker appears
- **AND** validation MUST NOT require `tools/boot_debug.py` automatic keyboard scancode injection during this change
- **AND** if runtime smoke cannot run, validation records the missing dependency and remaining keyboard/TTY runtime risk
