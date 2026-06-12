from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/common.lua',
            ROOT / 'xmake/boot_artifacts.lua',
            ROOT / 'xmake/user_package.lua',
            ROOT / 'xmake/runtime.lua',
            ROOT / 'xmake/kernel.lua',
            ROOT / 'xmake/run_targets.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def keyboard_handler_body() -> str:
    isr = read_source('kernel/core/irq/isr.cc')
    start = isr.index('implement_isr(keyboard)')
    end = isr.index('void init_isr_timer()')
    return isr[start:end]


def test_keyboard_irq1_handoff_preserves_dispatch_eoi_boundary() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard);')
    unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')
    # Stage 19: keyboard IRQ1 is unmasked unconditionally (no BIGOS_KEYBOARD_SMOKE
    # guard) so the default-boot interactive /bin/sh can read from the TTY ring.
    assert '#ifdef BIGOS_KEYBOARD_SMOKE' not in isr
    assert register_index < unmask_index

    body = keyboard_handler_body()
    assert 'const uint8_t scancode = inb(PS2_KEYBOARD_DATA_PORT);' in body
    assert 'bigos::input::handle_keyboard_scancode(scancode);' in body
    assert 'send_eoi' not in body
    assert interrupt.count('driver::irqchip::i8259::send_eoi') == 1


def test_keyboard_isr_body_stays_irq_context_safe() -> None:
    body = keyboard_handler_body()

    forbidden_tokens = (
        'kprintf',
        'kput',
        'serial_puts',
        'kmalloc',
        'alloc_kernel_pages',
        'free(',
        'mdelay',
        'sleep',
        'wait',
        'filesystem',
        'scheduler',
        'syscall',
    )
    for token in forbidden_tokens:
        assert token not in body


def test_tty_and_keyboard_are_ready_before_irq_enable() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    isr = read_source('kernel/core/irq/isr.cc')

    init_tty_index = kernel.index('bigos::terminal::init_tty();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    keyboard_register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard);')
    keyboard_unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')

    assert init_tty_index < init_irq_index < enable_irq_index
    assert keyboard_register_index < keyboard_unmask_index


def test_scancode_decoder_covers_minimal_set1_mapping_and_modifiers() -> None:
    keyboard = read_source('kernel/core/terminal/keyboard.cc')

    expected_mappings = (
        "key(0x01, '\\x1b')",
        "key(0x02, '1', '!')",
        "key(0x0e, '\\b')",
        "key(0x0f, '\\t')",
        "key(0x10, 'q', 'Q', 0x11)",
        "key(0x1c, '\\n')",
        "key(0x27, ';', ':')",
        "key(0x2b, '\\\\', '|', 0x1c)",
        "key(0x39, ' ')",
    )
    for mapping in expected_mappings:
        assert mapping in keyboard

    assert 'SCANCODE_LEFT_SHIFT = 0x2a' in keyboard
    assert 'SCANCODE_RIGHT_SHIFT = 0x36' in keyboard
    assert 'SCANCODE_CTRL = 0x1d' in keyboard
    assert 'SCANCODE_ALT = 0x38' in keyboard
    assert 'if (update_modifier(base_scancode, !released))' in keyboard
    assert 'record_unsupported();' in keyboard


def test_tty_ring_buffer_is_fixed_capacity_fifo_and_drops_new_input() -> None:
    tty_h = read_source('include/bigos/tty.h')
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'TTY_INPUT_CAPACITY = 128' in tty_h
    assert 'char buffer[TTY_INPUT_CAPACITY];' in tty
    assert 'const size_t next = next_index(head);' in tty
    assert 'if (next == g_input.tail)' in tty
    assert '++g_input.dropped;' in tty
    assert 'g_input.buffer[head] = ch;' in tty
    assert '*out = g_input.buffer[tail];' in tty
    assert 'while (count < capacity && read_char(&out[count]))' in tty

    enqueue_body = tty[tty.index('bool enqueue_input') : tty.index('bool read_char')]
    forbidden_tokens = ('kmalloc', 'alloc_kernel_pages', 'while (true)', 'sleep_for', 'read_char_blocking')
    for token in forbidden_tokens:
        assert token not in enqueue_body


def test_tty_blocking_consumer_is_additive_and_uses_wait_queue() -> None:
    tty_h = read_source('include/bigos/tty.h')
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'bool read_char(char *out) noexcept;' in tty_h
    assert 'size_t drain(char *out, size_t capacity) noexcept;' in tty_h
    assert 'int read_char_blocking(char *out, timer::tick_t timeout_ticks = 0) noexcept;' in tty_h
    assert 'sched::WaitQueue g_input_wait;' in tty
    assert 'sched::init_wait_queue(&g_input_wait);' in tty
    assert 'sched::wake_one(&g_input_wait);' in tty
    assert 'sched::wait_queue_wait_until(&g_input_wait, &input_available, nullptr, timeout_ticks)' in tty


def test_console_api_wraps_vga_without_serial_mirroring() -> None:
    console_h = read_source('include/bigos/console.h')
    console = read_source('kernel/core/terminal/console.cc')
    vga = read_source('kernel/drivers/video/vga.cc')
    io = read_source('kernel/core/bigos/io.cc')

    assert 'void console_put(char ch) noexcept;' in console_h
    assert 'void console_write(const char *s) noexcept;' in console_h
    assert 'driver::video::vga::write(ch);' in console
    assert 'serial_puts' not in console
    assert "if (__ch == '\\r')" in vga
    assert "if (__ch == '\\b')" in vga
    assert 'void bigos::kput(char c)' in io
    assert 'driver::video::vga::write(c);' in io
