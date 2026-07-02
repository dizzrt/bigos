# TTY, Console, And Keyboard Input

BigOS input covers x86_64 PS/2 set-1 keyboard input through the current PIC/APIC routing paths and the selected default runtime console backend. Its goal is a minimal, verifiable keyboard-to-TTY handoff and an ordinary runtime text output entry. It is not a full terminal, shell, or user-mode input subsystem.

## Input Data Flow

```text
keyboard IRQ1
  -> irq_dispatch()
  -> isr_keyboard()
  -> inb(0x60)
  -> input::handle_keyboard_scancode()
  -> PS/2 set-1 bounded decode
  -> terminal::enqueue_input() / terminal::enqueue_input_record()
  -> fixed TerminalInputRecord ring
  -> default terminal mode gate
  -> canonical read_char()/read_char_blocking() or raw read_raw_available_blocking()
  -> default user stdin when fd 0 has no installed file
```

The keyboard ISR reads one scancode byte, updates fixed decoder state, and enqueues supported characters or bounded terminal-control events into the TTY input buffer as fixed-size terminal input records. On a successful enqueue, the TTY layer may wake one blocked reader through the bounded scheduler wakeup helper. The ISR does not call `kprintf()`, `kput()`, VGA/serial output, dynamic allocation, blocking waits, `mdelay()`, filesystem, syscall, user-mode paths, direct context switching, or console viewport redraw.

## Scancode Policy

The current decoder supports a minimal US-layout PS/2 set 1 subset:

- Printable letters, digits, common symbols, and space.
- Enter, Backspace, Tab, and Escape.
- Shift, Ctrl, and Alt make/break state updates; modifier scancodes do not emit characters themselves.
- Ctrl emits C0 control characters for representative letters and a small set of control keys.
- `0xe0` extended arrow keys, Home, End, Delete, PageUp, and PageDown are represented as bounded terminal-control events that expand to documented ANSI escape byte sequences for the foreground program.
- `Shift+PageUp` and `Shift+PageDown` are the reserved kernel scrollback shortcuts. They are consumed as private kernel scrollback events and never leak PageUp/PageDown escape bytes to stdin.

Unsupported extended scancode sequences and unmapped scancodes are counted as unsupported and dropped. The decoder does not panic, allocate, block, redraw VGA output, or write unknown scancodes into the TTY buffer.

## TTY Input Buffer

The TTY input buffer is a static fixed-capacity ring buffer with capacity `TTY_INPUT_CAPACITY`. The IRQ producer writes through `terminal::enqueue_input()` or `terminal::enqueue_input_record()`; non-interrupt consumers may read the raw bounded record through `terminal::read_input_record()` or use the byte-compatible `terminal::read_char()` and `terminal::drain()` helpers.

Each `TerminalInputRecord` is either a character record or a control record. The bounded control subset is line end, backspace, delete-like, EOF-like, interrupt-like, navigation-key events, private kernel scrollback PageUp/PageDown events, and unsupported control. `\r` is normalized to line end for the byte-compatible consumer, EOF-like input becomes a deterministic empty-read result on default console stdin, interrupt-like input is delivered as the bounded `0x03` byte for the shell to cancel the current line, navigation controls expand to fixed escape sequences, `Shift+PageUp`/`Shift+PageDown` scrollback controls are consumed by non-interrupt character consumers to adjust the console viewport, and unsupported controls are consumed as deterministic no-ops by the terminal consumer.

Navigation sequence policy:

- Arrow keys: `ESC [ A`, `ESC [ B`, `ESC [ C`, `ESC [ D`.
- Home and End: `ESC [ H`, `ESC [ F`.
- Delete: `ESC [ 3 ~`.
- PageUp and PageDown: `ESC [ 5 ~`, `ESC [ 6 ~`.
- Sequence expansion uses fixed pending state in the TTY consumer. If the ring cannot accept the fixed control record, the whole key event is dropped and no partial escape sequence is exposed.

Overflow is deterministic: when the ring buffer is full, new input is dropped and the drop counter increments; unread input is not overwritten. Empty-buffer reads return `false` or `0` and do not sleep, wait for the scheduler, or depend on processes or user mode.

## Terminal Input Mode

The single default console terminal owns a fixed-size BigOS terminal mode state.
It is initialized to canonical mode by `terminal::init_tty()` and does not depend
on dynamic allocation, filesystem access, userland progress, or the selected
display backend.

Canonical mode is the default shell mode. It preserves the existing line-oriented
behavior: ordinary printable input is returned one byte at a time to the shell,
newline and backspace/delete-like feedback are handled by the non-interrupt
consumer, EOF-like input can become an empty read, interrupt-like input can
deliver bounded `SIGINT` to the current foreground group, and supported
scrollback controls may be consumed as console viewport operations.

Raw mode is a BigOS-specific input ownership boundary for foreground programs.
When fd `0` still uses the default console fast path, raw reads return one or
more currently available bytes after at least one byte is present; they do not
wait to fill the whole user buffer or wait for Enter. Raw mode does not perform
ordinary echo, does not convert EOF-like input to an empty read, does not
automatically signal the foreground group for Ctrl-C, and does not consume
supported default navigation keys as scrollback. Arrow keys, Home/End, Delete,
PageUp, and PageDown are exposed as bounded ANSI escape byte sequences for
userland ownership; only `Shift+PageUp` and `Shift+PageDown` remain kernel
scrollback shortcuts.

Mode changes use `SYS_TCGETMODE` and `SYS_TCSETMODE`, exposed in libc as
`bigos_tcgetmode()` and `bigos_tcsetmode()` with `struct bigos_terminal_mode`.
The object carries size, version, mode, and flags fields; unknown sizes,
versions, flags, or modes fail deterministically and leave the old mode intact.
Setting mode requires the caller to be in the current default terminal
foreground process group. A session leader recovery path may restore only
canonical mode, so `/bin/sh` can recover after a foreground command exits or
fails. `fork` and `execve` do not create private terminal mode objects; child
and replacement images observe the same default terminal state.

This interface is intentionally not POSIX `termios`: it does not expose baud
rates, `VMIN/VTIME`, serial line discipline, pseudo-terminals, terminal
databases, multiple terminal devices, background read/write control, or complete
job-control behavior.

## Blocking Consumer

blocking primitives and timer ownership capability adds `terminal::read_char_blocking()` as an additive non-interrupt API. It first tries the existing non-blocking `read_char()` path. If the input buffer is empty, it waits on the TTY input wait queue through `sched::wait_queue_wait_until()`, using a predicate checked with IRQs disabled so a producer wakeup cannot be missed between the empty check and enqueue.

The blocking API is valid only from ordinary running kernel-thread context where `sched::can_block()` succeeds. It returns `1` when it writes a character to the caller buffer, `0` for the bounded EOF-like terminal event, or a deterministic negative wait error such as timeout, invalid argument, or forbidden blocking context. Existing `read_char()` and `drain()` behavior remains non-blocking and does not depend on scheduler progress.

The automated blocking smoke uses a synthetic producer that calls `terminal::enqueue_input()` and therefore exercises the same TTY wakeup path without requiring manual keyboard input. Manual keyboard validation remains optional and should record emulator input capability when used.

Interactive console usability connects the same blocking consumers to default user stdin through a standard `vfs::File`. The global terminal is split into a long-lived "device" layer (the input ring and wait queue) and a per-open "handle" layer (`vfs::File`), and the terminal handle's read op enters the existing blocking terminal read path. When a user process reads fd `0` and that descriptor holds the terminal handle, `SYS_READ` routes through the fd table to `file->ops->read`, which blocks on the TTY ring and returns canonical bytes or raw currently available bytes according to the terminal mode. If fd `0` is replaced by a pipe or file through `dup2()` or redirection, reads use that descriptor's ops instead of the terminal handle.

## Console Output Boundary

Ordinary runtime text output uses the default terminal sink over `terminal::default_terminal_write()`, which wraps `terminal::console_put()` and `terminal::console_write()`. The runtime console owns fixed-capacity cell storage, display attributes, a bounded ANSI/CSI parser, UTF-8 decoder state, cursor position, saved cursor coordinates, a 256-line in-kernel scrollback buffer, viewport policy, and clear policy. Its visible columns and rows come from the selected internal render backend: VGA text remains the fixed 80x25 Legacy fallback, while a UEFI framebuffer text backend may be selected only after validated framebuffer metadata, an explicit `map_device_mmio()` mapping, a supported 32-bit RGBX/BGRX pixel format, a usable glyph lookup view, and a bounded complete-cell grid computed from framebuffer geometry. User writes to fd `1` or fd `2` route through the fd table to the terminal handle's write op (`file->ops->write`); that op preserves the existing bounded serial write marker before the default console write, so headless smokes can continue to observe default userland progress. Descriptors redirected to a pipe or file route to that descriptor's ops instead. The console API itself does not mirror to COM1 serial by default; serial stays reserved for bounded markers, smokes, and fatal diagnostics.

Basic control-character behavior:

- `\n`: move to the beginning of the next line.
- `\r`: move to the beginning of the current line.
- `\t`: advance to the next 4-column tab stop using deterministic blank cells.
- `\b`: erase the previous logical character, including both cells of a double-width character when needed.
- Supported ANSI/CSI output subset: SGR reset/default, foreground `30-37`, background `40-47`, bright foreground `90-97`, bright background `100-107`, cursor movement `CUU/CUD/CUF/CUB`, one-based `CUP/HVP`, erase display `ED`, erase line `EL`, `CSI s/u`, and `ESC 7/8`.
- Unsupported or malformed escape sequences: the bounded parser resets to ordinary text state or discards the unsupported control sequence; subsequent ordinary UTF-8 output resumes without requiring a terminal reset.

When output moves past the last visible row, the runtime console updates its owned scrollback state and asks the backend to redraw the complete visible viewport. The VGA backend uses the hardware text cursor; the framebuffer backend renders glyph pixels from console-owned Unicode codepoint cells over its dynamic visible grid and draws a software cursor from backend state without storing cursor bytes in scrollback. The reserved `Shift+PageUp` and `Shift+PageDown` shortcuts use a bounded step derived from the current visible row count. New output while viewing history does not corrupt retained history or force the viewport back to the bottom. The supported console clear path clears through the selected backend, discards retained runtime scrollback, and resets the viewport to the bottom; on framebuffer this clear covers the full validated mapped framebuffer so firmware pixels outside the text grid do not remain visible.

The runtime console decodes ordinary output as bounded UTF-8 after escape parser classification, stores Unicode codepoint cells, and records single-width, double-width leading, double-width trailing, blank, or replacement cell roles. Newly written cells copy the current SGR-derived display attributes; existing cells keep their stored attributes until rewritten or erased. Glyph width uses the kernel glyph lookup width class when available: half-width glyphs occupy one cell and full-width glyphs occupy two cells. Missing or invalid codepoints prefer the `U+FFFD` replacement glyph, then deterministically degrade to `?` or blank if that glyph is unavailable. The framebuffer backend renders those console-owned cells and attributes through glyph lookup and deterministic RGB colors; the Legacy VGA text backend directly displays printable ASCII, maps the same foreground/background attributes into a deterministic VGA color byte, and degrades non-ASCII or trailing cells. Render backends do not own ANSI parser, UTF-8 decoder, scrollback, TTY input, or terminal mode state.

`kput()`, `kputs()`, `kprintf()`, `serial_puts()`, and fatal/page-fault/memory self-test marker paths keep early direct-output semantics and do not depend on TTY initialization, framebuffer console initialization, glyph lookup availability, or input-buffer state.

## Initialization Order

`kernel()` currently runs:

```text
VGA clear
serial_init()
init_mem()
optional BIGOS_MM_SELF_TEST
optional BIGOS_USER_VMEM_SMOKE
terminal::init_tty()
irq::initIRQ()
optional BIGOS_PAGE_FAULT_SMOKE trigger
irq::enableIRQ()
normal boot marker
optional syscall / scheduler / user-program smoke
sched::start()  (idle thread owns halt; replaces the bare hlt loop)
```

`serial_init()` explicitly brings up early COM1 on the ordinary boot path, so default serial markers no longer depend on indirect initialization from `mm_self_test()` or similar paths. `terminal::init_tty()` initializes the input ring, console-ready flag, and keyboard decoder state. `irq::initIRQ()` then initializes IDT/PIC and registers timer/keyboard handlers. PIC initialization masks all IRQ lines by default. In the APIC default-delivery configuration, keyboard IRQ1 is routed through IOAPIC to the initialized online BSP and completes with LAPIC EOI; in the documented BSP-only fallback, keyboard IRQ1 is unmasked on the PIC path after the handler is registered. Automated headless validation still does not require manual keyboard input.

## Minimal Interactive Console

The default interactive shell path intentionally stays below POSIX terminal scope:

- Keyboard IRQ1 only decodes and enqueues fixed-size input records, then performs the bounded TTY wakeup.
- Default navigation keys are fixed-size TTY control records that expand to ANSI escape bytes for userland; only `Shift+PageUp` and `Shift+PageDown` are private kernel scrollback controls. Viewport movement and whole-screen redraw occur only when a non-interrupt terminal consumer processes those private controls.
- Printable input, newline feedback, backspace feedback, EOF-like exit, interrupt-like line cancellation, and unsupported-control no-op behavior are produced by the non-interrupt terminal or shell consumer after `read(0, ...)` returns.
- The default terminal keeps one numeric foreground `pgid`. It is queried and
  changed only from ordinary syscall/user-process context; IRQ1 never performs
  process-group traversal, shell policy, allocation, blocking, or filesystem I/O.
- Interrupt-like input still returns the bounded `0x03` byte to the consumer and
  also attempts bounded `SIGINT` delivery to the current foreground group while
  canonical mode is active. Raw mode delivers the byte to userland without
  automatic signal delivery. If the foreground group is missing or empty, the
  result is deterministic no-op/error handling, never a dangling process-object
  dereference.
- `/bin/sh` restores the single default terminal to canonical mode when it
  regains the foreground group after a foreground command or pipeline.
- `/bin/sh` shows its deterministic `$ ` prompt only when both fd `0` and fd `1`
  report a terminal through `isatty()` (an `fstat`-backed character-device
  check); pipes or redirected regular files report non-terminal and suppress the
  prompt.
- stdout and stderr are visible through the selected default console render backend through fd `1` and fd `2` when those descriptors are not redirected.

## Standard Descriptors As Terminal Files

When a fresh top-level user process enters ring3, the kernel installs one shared
terminal `vfs::File` (the "handle" layer) at fd `0`, fd `1`, and fd `2`. The
three descriptors point at the same handle, so its reference count reaches three
and a single close does not free it. The handle is read/write (`readable =
writable = true`), `close_on_exec` is false, and its `private_data` points at the
long-lived global terminal "device" layer. `fork` shares the handle through the
ordinary fd-table copy (one retain per duplicated descriptor); `exec` keeps the
existing fd table (only `close_on_exec` descriptors are dropped), so the standard
terminal descriptors survive `exec` without reinstallation. The handle follows
the same `retain`/`release` lifecycle as pipes and sockets; its close op is a
device-layer no-op, so releasing the last reference frees only the `vfs::File`
structure and never the global input ring or wait queue. `fstat` on the terminal
handle reports a character device (`S_IFCHR`), which is how userland `isatty()`
distinguishes it from regular files and pipes without a new syscall number.

## Non-Goals

This path does not implement multiple TTYs, complete xterm/VT100/VT220 behavior,
command history, complete POSIX `termios`, pseudo-terminals, complete job control, background
read/write control, USB HID, full graphical terminal behavior, locale,
Unicode normalization, grapheme clusters, shaping, input methods, persistent or
unbounded history, USB HID, or internationalized keyboard layouts. The minimal fd
integration covers only the default console fast paths for the current userland and
does not introduce `/dev/tty`, a general character-device filesystem, async I/O,
or full POSIX terminal reads.
