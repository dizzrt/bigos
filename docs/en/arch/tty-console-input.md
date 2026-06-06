# TTY, Console, And Keyboard Input

BigOS stage 2 input covers only single-core x86_64, i8259 PIC, PS/2 set-1 keyboard, and VGA text mode. Its goal is a minimal, verifiable keyboard-to-TTY handoff and an ordinary runtime text output entry. It is not a full terminal, shell, or user-mode input subsystem.

## Input Data Flow

```text
keyboard IRQ1
  -> irq_dispatch()
  -> isr_keyboard()
  -> inb(0x60)
  -> input::handle_keyboard_scancode()
  -> PS/2 set-1 bounded decode
  -> terminal::enqueue_input()
  -> non-interrupt consumer read_char()/drain()
```

The keyboard ISR reads one scancode byte, updates fixed decoder state, and enqueues supported characters into the TTY input buffer. The ISR does not call `kprintf()`, `kput()`, VGA/serial output, dynamic allocation, blocking waits, `mdelay()`, filesystem, scheduler, syscall, or user-mode paths.

## Scancode Policy

The current decoder supports a minimal US-layout PS/2 set 1 subset:

- Printable letters, digits, common symbols, and space.
- Enter, Backspace, Tab, and Escape.
- Shift, Ctrl, and Alt make/break state updates; modifier scancodes do not emit characters themselves.
- Ctrl emits C0 control characters for representative letters and a small set of control keys.

Extended scancode prefixes `0xe0`/`0xe1` and unmapped scancodes are counted as unsupported and dropped. The decoder does not panic, allocate, block, or write unknown scancodes into the TTY buffer.

## TTY Input Buffer

The TTY input buffer is a static fixed-capacity ring buffer with capacity `TTY_INPUT_CAPACITY`. The IRQ producer writes through `terminal::enqueue_input()`; non-interrupt consumers read through `terminal::read_char()` or `terminal::drain()`.

Overflow is deterministic: when the ring buffer is full, new input is dropped and the drop counter increments; unread input is not overwritten. Empty-buffer reads return `false` or `0` and do not sleep, wait for the scheduler, or depend on processes or user mode.

## Console Output Boundary

Ordinary runtime text output uses `terminal::console_put()` and `terminal::console_write()`. The current backend writes only VGA text mode and does not mirror to COM1 serial by default. Serial stays reserved for bounded markers, smokes, and fatal diagnostics.

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

`serial_init()` explicitly brings up early COM1 on the ordinary boot path, so default serial markers no longer depend on indirect initialization from `mm_self_test()` or similar paths. `terminal::init_tty()` initializes the input ring, console-ready flag, and keyboard decoder state. `irq::initIRQ()` then initializes IDT/PIC and registers timer/keyboard handlers. PIC initialization masks all IRQ lines by default; timer IRQ0 is still unmasked by timer initialization, while keyboard IRQ1 is unmasked only when `BIGOS_KEYBOARD_SMOKE` is explicitly enabled. Default boot therefore does not depend on keyboard input.

## Non-Goals

This stage does not implement a scheduler, blocking reads, wait queues, multiple TTYs, full ANSI/VT terminal behavior, line editing, command history, shell, syscall, user mode, USB HID, APIC/IOAPIC, SMP, or internationalized keyboard layouts.
