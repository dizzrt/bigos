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
    end = isr.index('implement_isr(scheduler_nudge)')
    return isr[start:end]


def test_keyboard_irq1_handoff_preserves_dispatch_eoi_boundary() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Pic);')
    unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')
    # default interactive userland baseline: keyboard IRQ1 is unmasked unconditionally (no BIGOS_KEYBOARD_SMOKE
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
    keyboard_register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Pic);')
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


def test_scancode_decoder_classifies_scrollback_extended_keys_as_control_records() -> None:
    keyboard = read_source('kernel/core/terminal/keyboard.cc')
    tty_h = read_source('include/bigos/tty.h')

    expected_controls = (
        'ScrollPageUp',
        'ScrollPageDown',
        'ScrollHome',
        'ScrollEnd',
    )
    for control in expected_controls:
        assert control in tty_h
        assert f'TerminalControl::{control}' in keyboard

    assert 'SCANCODE_EXTENDED_E0 = 0xe0' in keyboard
    assert 'SCANCODE_HOME = 0x47' in keyboard
    assert 'SCANCODE_PAGE_UP = 0x49' in keyboard
    assert 'SCANCODE_END = 0x4f' in keyboard
    assert 'SCANCODE_PAGE_DOWN = 0x51' in keyboard
    assert 'bool make_scroll_record(uint8_t base_scancode, TerminalInputRecord *out) noexcept' in keyboard
    assert '*out = {TerminalInputKind::Control, 0, control};' in keyboard
    assert 'if (make_scroll_record(base_scancode, out))' in keyboard
    assert '(void)terminal::enqueue_input_record(record);' in keyboard


def test_tty_ring_buffer_is_fixed_capacity_fifo_and_drops_new_input() -> None:
    tty_h = read_source('include/bigos/tty.h')
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'TTY_INPUT_CAPACITY = 128' in tty_h
    assert 'TerminalInputRecord buffer[TTY_INPUT_CAPACITY];' in tty
    assert 'const size_t next = next_index(head);' in tty
    assert 'if (next == g_input.tail)' in tty
    assert '++g_input.dropped;' in tty
    assert 'g_input.buffer[head] = record;' in tty
    assert '*out = g_input.buffer[tail];' in tty
    assert 'while (count < capacity && read_char(&out[count]))' in tty

    enqueue_body = tty[tty.index('bool enqueue_input') : tty.index('bool read_char')]
    forbidden_tokens = ('kmalloc', 'alloc_kernel_pages', 'while (true)', 'sleep_for', 'read_char_blocking')
    for token in forbidden_tokens:
        assert token not in enqueue_body


def test_tty_char_consumers_handle_scrollback_events_without_byte_leakage() -> None:
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'case TerminalControl::ScrollPageUp:' in tty
    assert 'console_scroll_page_up();' in tty
    assert 'case TerminalControl::ScrollPageDown:' in tty
    assert 'console_scroll_page_down();' in tty
    assert 'case TerminalControl::ScrollHome:' in tty
    assert 'console_scroll_home();' in tty
    assert 'case TerminalControl::ScrollEnd:' in tty
    assert 'console_scroll_end();' in tty
    assert tty.count('return ConsumeResult::Ignored;') >= 5


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
    assert 'void console_scroll_page_up() noexcept;' in console_h
    assert 'ConsoleCell lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_WIDTH];' in console
    assert 'CONSOLE_SCROLLBACK_LINES = 256' in console
    assert 'bigos::device::fill_video_text_cell' in console
    assert 'serial_puts' not in console
    assert "if (__ch == '\\r')" in vga
    assert "if (__ch == '\\b')" in vga
    assert 'void bigos::kput(char c)' in io
    assert 'bigos::device::write_video_text(c);' in io


def test_direct_kernel_vga_diagnostics_switch_from_user_cr3() -> None:
    io = read_source('kernel/core/bigos/io.cc')

    assert '#include <bigos/arch_vm_user_boundary.h>' in io
    assert '#include <bigos/proc.h>' in io
    assert 'switch_to_current_kernel_cr3()' in io
    assert 'process->kernel_address_space_root' in io
    assert 'bigos::arch::vm_user::activate_kernel_address_space(process->kernel_address_space_root);' in io
    assert 'restore_cr3(restore_root);' in io


def test_vga_text_backend_bounds_cursor_and_scrolls_visible_screen() -> None:
    vga = read_source('kernel/drivers/video/vga.cc')
    vga_h = read_source('include/drivers/video/vga.h')

    assert 'static DisplayMode mode_3{80, 25, true};' in vga
    assert 'static uint64_t frame_buffer_base = 0xb8000;' in vga
    assert 'static inline uint16_t clamp_pos(uint16_t __pos)' in vga
    assert '__pos = clamp_pos(__pos);' in vga
    assert 'static void scroll_up_one(uint8_t __color)' in vga
    assert 'clear_row(mode->height - 1, __color);' in vga
    assert 'static uint16_t newline_pos(uint16_t __pos, uint8_t __color)' in vga
    assert 'static uint16_t advance_pos(uint16_t __pos, uint8_t __color)' in vga
    assert "write(' ', get_cursor(), __color);" in vga
    assert 'void fill_cell(uint8_t __x, uint8_t __y, char __ch, uint8_t __color = VT_COLOR_NORMAL);' in vga_h


def test_console_scrollback_state_viewport_follow_and_clear_policy() -> None:
    console = read_source('kernel/core/terminal/console.cc')

    assert 'CONSOLE_WIDTH = 80' in console
    assert 'CONSOLE_HEIGHT = 25' in console
    assert 'CONSOLE_SCROLLBACK_LINES = 256' in console
    assert 'uint64_t oldest_line;' in console
    assert 'uint64_t current_line;' in console
    assert 'uint64_t viewport_top;' in console
    assert 'uint8_t cursor_x;' in console
    assert 'uint64_t bottom_viewport_top() noexcept' in console
    assert 'bool viewport_at_bottom() noexcept' in console
    assert 'void render_viewport() noexcept' in console
    assert 'bigos::device::fill_video_text_cell' in console
    assert 'bigos::device::set_video_text_cursor(g_console.cursor_x, cursor_y);' in console
    assert 'g_console.current_line + 1 - CONSOLE_SCROLLBACK_LINES' in console
    assert 'if (follow)' in console
    assert 'console_scroll_page_up' in console
    assert 'console_scroll_page_down' in console
    assert 'console_scroll_home' in console
    assert 'console_scroll_end' in console
    assert 'Clear-screen discards retained runtime scrollback' in console


def test_default_user_stdout_and_stderr_reach_visible_console() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    assert '#include <bigos/console.h>' in syscall
    assert '(__fd == 1 || __fd == 2)' in syscall
    assert 'bigos::proc::file_for_fd_current((uint32_t)__fd) == nullptr' in syscall
    assert 'serial_puts("BIGOS_USER_WRITE_SYSCALL\\n");' in syscall
    assert 'serial_puts(bounded);' in syscall
    assert 'bigos::proc::copy_current_user_buffer(__buffer, bounded, __len)' in syscall
    assert 'bigos::proc::validate_user_buffer(__buffer, __len)' in syscall
    assert 'bigos::terminal::default_terminal_write(bounded);' in syscall
    assert 'bigos::arch::vm_user::activate_address_space(active_root);' in syscall


def test_keyboard_irq_echo_stays_in_non_interrupt_shell_consumer() -> None:
    body = keyboard_handler_body()
    shell = read_source('user/sh/sh.c')

    assert 'write_all(1, echo);' in shell
    assert 'write_all(1, "\\b");' in shell
    assert 'write_all(1, "\\n");' in shell
    assert 'should_echo_input' in shell
    assert 'write_all' not in body
    assert 'console_write' not in body
    assert 'console_scroll' not in body
    assert 'fill_cell' not in body
