# TTY, Console, And Keyboard Input

BigOS input covers only single-core x86_64, i8259 PIC, PS/2 set-1 keyboard, and VGA text mode. Its goal is a minimal, verifiable keyboard-to-TTY handoff and an ordinary runtime text output entry. It is not a full terminal, shell, or user-mode input subsystem.

## Input Data Flow

```text
keyboard IRQ1
  -> irq_dispatch()
  -> isr_keyboard()
  -> inb(0x60)
  -> input::handle_keyboard_scancode()
  -> PS/2 set-1 bounded decode
  -> terminal::enqueue_input()
  -> fixed TerminalInputRecord ring
  -> non-interrupt consumer read_input_record()/read_char()/drain()
  -> optional blocking consumer read_char_blocking()
  -> default user stdin when fd 0 has no installed file
```

The keyboard ISR reads one scancode byte, updates fixed decoder state, and enqueues supported characters into the TTY input buffer as fixed-size terminal input records. On a successful enqueue, the TTY layer may wake one blocked reader through the bounded scheduler wakeup helper. The ISR does not call `kprintf()`, `kput()`, VGA/serial output, dynamic allocation, blocking waits, `mdelay()`, filesystem, syscall, user-mode paths, or direct context switching.

## Scancode Policy

The current decoder supports a minimal US-layout PS/2 set 1 subset:

- Printable letters, digits, common symbols, and space.
- Enter, Backspace, Tab, and Escape.
- Shift, Ctrl, and Alt make/break state updates; modifier scancodes do not emit characters themselves.
- Ctrl emits C0 control characters for representative letters and a small set of control keys.

Extended scancode prefixes `0xe0`/`0xe1` and unmapped scancodes are counted as unsupported and dropped. The decoder does not panic, allocate, block, or write unknown scancodes into the TTY buffer.

## TTY Input Buffer

The TTY input buffer is a static fixed-capacity ring buffer with capacity `TTY_INPUT_CAPACITY`. The IRQ producer writes through `terminal::enqueue_input()` or `terminal::enqueue_input_record()`; non-interrupt consumers may read the raw bounded record through `terminal::read_input_record()` or use the byte-compatible `terminal::read_char()` and `terminal::drain()` helpers.

Each `TerminalInputRecord` is either a character record or a control record. The bounded control subset is line end, backspace, delete-like, EOF-like, interrupt-like, and unsupported control. `\r` is normalized to line end for the byte-compatible consumer, EOF-like input becomes a deterministic empty-read result on default console stdin, interrupt-like input is delivered as the bounded `0x03` byte for the shell to cancel the current line, and unsupported controls are consumed as deterministic no-ops by the terminal consumer.

Overflow is deterministic: when the ring buffer is full, new input is dropped and the drop counter increments; unread input is not overwritten. Empty-buffer reads return `false` or `0` and do not sleep, wait for the scheduler, or depend on processes or user mode.

## Blocking Consumer

blocking primitives and timer ownership capability adds `terminal::read_char_blocking()` as an additive non-interrupt API. It first tries the existing non-blocking `read_char()` path. If the input buffer is empty, it waits on the TTY input wait queue through `sched::wait_queue_wait_until()`, using a predicate checked with IRQs disabled so a producer wakeup cannot be missed between the empty check and enqueue.

The blocking API is valid only from ordinary running kernel-thread context where `sched::can_block()` succeeds. It returns `1` when it writes a character to the caller buffer, `0` for the bounded EOF-like terminal event, or a deterministic negative wait error such as timeout, invalid argument, or forbidden blocking context. Existing `read_char()` and `drain()` behavior remains non-blocking and does not depend on scheduler progress.

The automated blocking smoke uses a synthetic producer that calls `terminal::enqueue_input()` and therefore exercises the same TTY wakeup path without requiring manual keyboard input. Manual keyboard validation remains optional and should record emulator input capability when used.

Interactive console usability connects the same blocking consumer to default user stdin: when a user process reads fd `0` and no file or pipe is installed there, `SYS_READ` blocks on the TTY ring and returns one bounded byte to user space. If fd `0` is replaced by a pipe or file through `dup2()` or redirection, reads use the normal fd/VFS path instead of the default console.

## Console Output Boundary

Ordinary runtime text output uses the default terminal sink over `terminal::default_terminal_write()`, which wraps the existing `terminal::console_put()` and `terminal::console_write()` VGA text-mode backend. Interactive console usability routes user writes to fd `1` or fd `2` through this visible console when no file or pipe is installed at that descriptor; redirected descriptors still use the normal fd/VFS path. The syscall path also preserves the existing bounded serial write marker so headless smokes can continue to observe default userland progress. The console API itself does not mirror to COM1 serial by default; serial stays reserved for bounded markers, smokes, and fatal diagnostics.

Basic control-character behavior:

- `\n`: move to the beginning of the next line.
- `\r`: move to the beginning of the current line.
- `\t`: advance by four character positions.
- `\b`: move back one cell when not at the start position and erase that cell.
- Unsupported escape sequences: ANSI/VT sequences are not parsed; Escape is written as an ordinary character or ignored by an upper layer.

`kput()`, `kputs()`, `kprintf()`, `serial_puts()`, and fatal/page-fault/memory self-test marker paths keep early direct-output semantics and do not depend on TTY initialization or input-buffer state.

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

`serial_init()` explicitly brings up early COM1 on the ordinary boot path, so default serial markers no longer depend on indirect initialization from `mm_self_test()` or similar paths. `terminal::init_tty()` initializes the input ring, console-ready flag, and keyboard decoder state. `irq::initIRQ()` then initializes IDT/PIC and registers timer/keyboard handlers. PIC initialization masks all IRQ lines by default; timer IRQ0 is still unmasked by timer initialization, while keyboard IRQ1 is unmasked for the default interactive shell path after the handler is registered. Automated headless validation still does not require manual keyboard input.

## Minimal Interactive Console

The default interactive shell path intentionally stays below POSIX terminal scope:

- Keyboard IRQ1 only decodes and enqueues fixed-size input records, then performs the bounded TTY wakeup.
- Printable input, newline feedback, backspace feedback, EOF-like exit, interrupt-like line cancellation, and unsupported-control no-op behavior are produced by the non-interrupt terminal or shell consumer after `read(0, ...)` returns.
- The default terminal keeps one numeric foreground `pgid`. It is queried and
  changed only from ordinary syscall/user-process context; IRQ1 never performs
  process-group traversal, shell policy, allocation, blocking, or filesystem I/O.
- Interrupt-like input still returns the bounded `0x03` byte to the consumer and
  also attempts bounded `SIGINT` delivery to the current foreground group. If the
  foreground group is missing or empty, the result is deterministic no-op/error
  handling, never a dangling process-object dereference.
- `/bin/sh` shows its deterministic `$ ` prompt only when fd `0` and fd `1` are still bound to the default console fast paths; pipes or redirected files suppress the prompt.
- stdout and stderr are visible on VGA text mode through fd `1` and fd `2` when those descriptors are not redirected.

## Non-Goals

This path does not implement multiple TTYs, full ANSI/VT terminal behavior,
command history, termios, complete job control, background read/write control,
USB HID, APIC/IOAPIC, SMP, or internationalized keyboard layouts. The minimal fd
integration covers only the default console fast paths for bounded userland and
does not introduce `/dev/tty`, a general character-device filesystem, async I/O,
or full POSIX terminal reads.
