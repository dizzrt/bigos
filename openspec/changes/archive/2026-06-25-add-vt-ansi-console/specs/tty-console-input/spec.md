## MODIFIED Requirements

### Requirement: Console output has a unified minimal API

BigOS SHALL expose a minimal console/TTY output API for ordinary kernel text output while preserving existing early diagnostic direct-output paths. The ordinary runtime console output API SHALL include the bounded ANSI/VT output subset selected by the `vt-ansi-console` capability.

#### Scenario: Console writes visible text

- **WHEN** kernel code writes a character or string through the console/TTY output API after initialization
- **THEN** BigOS emits the text to the selected runtime console backend
- **AND** BigOS does not mirror ordinary console text to COM1 serial by default
- **AND** BigOS keeps output behavior freestanding-safe without requiring heap allocation or hosted formatting

#### Scenario: Basic control characters are handled

- **WHEN** console output receives newline, carriage return, tab, or backspace
- **THEN** BigOS applies a documented minimal text-mode behavior for that control character
- **AND** the behavior MUST remain deterministic when the ANSI/VT parser is in ordinary text state

#### Scenario: Supported escape sequences are parsed by the runtime console

- **WHEN** ordinary runtime console output receives a supported ANSI/CSI sequence from the bounded console subset
- **THEN** BigOS MUST parse and apply the sequence through console-owned state
- **AND** the sequence MUST NOT be emitted as literal printable text

#### Scenario: Unsupported terminal sequences recover deterministically

- **WHEN** ordinary runtime console output receives an unsupported or invalid ANSI/VT escape sequence
- **THEN** BigOS MUST ignore, discard, or otherwise recover from that sequence according to documented bounded parser policy
- **AND** subsequent ordinary text MUST continue to display without requiring TTY reinitialization, process restart, or backend reset

#### Scenario: Early diagnostics remain independent

- **WHEN** early panic, page fault diagnostics, memory self-test markers, or other fatal paths emit deterministic markers
- **THEN** those paths MAY continue using direct VGA/COM1 output APIs
- **AND** they MUST NOT depend on TTY initialization, ANSI parser state, input buffer state, framebuffer console initialization, or userland fd state

#### Scenario: Existing direct output remains early-only

- **WHEN** this change extends the console/TTY output API
- **THEN** existing `kput()` and `kputs()` direct-output APIs retain their early diagnostic semantics
- **AND** ordinary runtime console output uses the console API rather than changing `kput()` or `kputs()` into ANSI-aware console wrappers

### Requirement: TTY input exposes terminal events without widening IRQ work

BigOS SHALL extend the TTY input path so the default terminal can represent printable input, a bounded set of control-character events, userland-visible terminal navigation escape sequences needed by shell/userland consumers, and `Shift+PageUp`/`Shift+PageDown` scrollback control events that are not exposed as stdin bytes. Keyboard IRQ1 MUST remain an IRQ-safe producer: it MAY classify and enqueue fixed-size input records, characters, fixed escape-sequence bytes, or fixed scrollback events, but it MUST NOT perform ordinary echo, terminal policy, shell cancellation, process signaling, console viewport redraw, or dynamic terminal state updates that require non-interrupt context.

#### Scenario: Control input is enqueued as bounded data

- **WHEN** keyboard IRQ1 observes a supported control-key combination for the default terminal
- **THEN** BigOS MUST enqueue a bounded character or input event representation into TTY-owned fixed-capacity state
- **AND** the IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, call `kprintf`, perform ordinary console output, or depend on user-mode progress

#### Scenario: Terminal policy remains outside IRQ

- **WHEN** EOF-like, interrupt-like, newline, backspace-like input, or navigation-key input is later consumed
- **THEN** canonical/raw terminal mode and the userland read path MUST decide the observable result
- **AND** keyboard IRQ1 MUST NOT directly kill processes, rewrite shell state, adjust large console viewport state, consume default navigation keys as scrollback operations, or emit ordinary prompt/echo output

#### Scenario: Navigation key sequence is bounded

- **WHEN** keyboard IRQ1 observes a supported arrow, Home, End, Delete-like, PageUp, or PageDown navigation key
- **THEN** BigOS MUST enqueue a documented fixed byte sequence or a fixed-size event that later expands to that sequence
- **AND** the operation MUST be bounded, allocation-free, and safe if the fixed TTY buffer is full

#### Scenario: Default navigation keys are userland input

- **WHEN** a foreground user program reads from the default console stdin path and a supported navigation key has been pressed
- **THEN** BigOS MUST deliver the documented ANSI escape sequence to that program
- **AND** BigOS MUST NOT consume the default navigation key as an implicit console scrollback operation

#### Scenario: Shift Page keys remain console scrollback controls

- **WHEN** `Shift+PageUp` or `Shift+PageDown` is pressed on the default console
- **THEN** BigOS MUST enqueue or deliver a bounded scrollback control event for non-interrupt console viewport handling
- **AND** userland `read(fd=0)` MUST NOT receive a PageUp/PageDown escape sequence or partial escape bytes for that key combination
- **AND** keyboard IRQ1 MUST NOT redraw the viewport directly

#### Scenario: Raw mode preserves navigation bytes

- **WHEN** the single default terminal is in BigOS raw mode and a supported navigation key sequence is available
- **THEN** `read(fd=0)` MUST return the sequence bytes through the existing raw input behavior
- **AND** raw delivery MUST NOT perform canonical editing, signal delivery, prompt feedback, or viewport scrolling for those bytes
