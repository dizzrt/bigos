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


def test_scancode_decoder_classifies_navigation_extended_keys_as_control_records() -> None:
    keyboard = read_source('kernel/core/terminal/keyboard.cc')
    tty_h = read_source('include/bigos/tty.h')

    expected_controls = (
        'NavigateUp',
        'NavigateDown',
        'NavigateRight',
        'NavigateLeft',
        'NavigateHome',
        'NavigateEnd',
        'NavigateDelete',
        'NavigatePageUp',
        'NavigatePageDown',
        'KernelScrollPageUp',
        'KernelScrollPageDown',
    )
    for control in expected_controls:
        assert control in tty_h
        assert f'TerminalControl::{control}' in keyboard

    assert 'SCANCODE_EXTENDED_E0 = 0xe0' in keyboard
    assert 'SCANCODE_HOME = 0x47' in keyboard
    assert 'SCANCODE_ARROW_UP = 0x48' in keyboard
    assert 'SCANCODE_PAGE_UP = 0x49' in keyboard
    assert 'SCANCODE_ARROW_LEFT = 0x4b' in keyboard
    assert 'SCANCODE_ARROW_RIGHT = 0x4d' in keyboard
    assert 'SCANCODE_END = 0x4f' in keyboard
    assert 'SCANCODE_ARROW_DOWN = 0x50' in keyboard
    assert 'SCANCODE_PAGE_DOWN = 0x51' in keyboard
    assert 'SCANCODE_DELETE = 0x53' in keyboard
    assert 'bool make_navigation_record(uint8_t base_scancode, TerminalInputRecord *out) noexcept' in keyboard
    assert '*out = {TerminalInputKind::Control, 0, control};' in keyboard
    assert 'g_state.shift ? TerminalControl::KernelScrollPageUp : TerminalControl::NavigatePageUp' in keyboard
    assert 'g_state.shift ? TerminalControl::KernelScrollPageDown : TerminalControl::NavigatePageDown' in keyboard
    assert 'if (make_navigation_record(base_scancode, out))' in keyboard
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


def test_terminal_mode_state_is_fixed_size_and_canonical_by_default() -> None:
    tty_h = read_source('include/bigos/tty.h')
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'TERMINAL_MODE_ABI_VERSION = 1' in tty_h
    assert 'TERMINAL_MODE_CANONICAL = 0' in tty_h
    assert 'TERMINAL_MODE_RAW = 1' in tty_h
    assert 'enum class TerminalInputMode : uint32_t' in tty_h
    assert 'struct TerminalMode' in tty_h
    assert 'TerminalInputMode g_input_mode = TerminalInputMode::Canonical;' in tty
    assert 'g_input_mode = TerminalInputMode::Canonical;' in tty
    assert 'mode.size != sizeof(TerminalMode)' in tty
    assert 'mode.version != TERMINAL_MODE_ABI_VERSION' in tty
    assert 'mode.flags != TERMINAL_MODE_FLAG_NONE' in tty
    assert 'return -bigos::EINVAL;' in tty

    mode_body = tty[tty.index('int64_t set_input_mode') : tty.index('uint32_t foreground_pgid')]
    for token in ('kmalloc', 'alloc_kernel_pages', 'bigos::vfs', 'console_render_backend'):
        assert token not in mode_body


def test_tty_char_consumers_expand_navigation_and_keep_shift_page_scrollback_private() -> None:
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'struct PendingSequenceState' in tty
    assert 'PendingSequenceState g_pending_sequence;' in tty
    assert 'bool set_navigation_sequence(TerminalControl control) noexcept' in tty
    for sequence in ('"\\x1b[A"', '"\\x1b[B"', '"\\x1b[C"', '"\\x1b[D"', '"\\x1b[H"', '"\\x1b[F"', '"\\x1b[3~"', '"\\x1b[5~"', '"\\x1b[6~"'):
        assert sequence in tty
    assert 'case TerminalControl::NavigatePageUp:' in tty
    assert 'case TerminalControl::NavigatePageDown:' in tty
    assert 'case TerminalControl::KernelScrollPageUp:' in tty
    assert 'console_scroll_page_up();' in tty
    assert 'case TerminalControl::KernelScrollPageDown:' in tty
    assert 'console_scroll_page_down();' in tty
    raw_body = tty[tty.index('bool consume_record_as_raw_char') : tty.index('bool mode_object_valid')]
    assert 'console_scroll_page_up' not in raw_body
    assert 'console_scroll_page_down' not in raw_body


def test_raw_mode_delivers_control_bytes_without_canonical_side_effects() -> None:
    tty_h = read_source('include/bigos/tty.h')
    tty = read_source('kernel/core/terminal/tty.cc')

    assert 'bool read_raw_char(char *out) noexcept;' in tty_h
    assert 'int read_raw_available_blocking(char *out, size_t capacity, timer::tick_t timeout_ticks = 0) noexcept;' in tty_h
    assert 'consume_record_as_raw_char' in tty
    assert '*out = 0x03;' in tty
    assert '*out = 0x04;' in tty
    assert 'set_pending_sequence("\\x1b[5~", 4);' in tty
    assert 'console_scroll_page_up();' in tty
    raw_body = tty[tty.index('bool consume_record_as_raw_char') : tty.index('bool mode_object_valid')]
    assert 'signal_process_group_from_current' not in raw_body
    assert 'console_scroll_page_up' not in raw_body
    assert 'console_scroll_page_down' not in raw_body
    # Raw/canonical selection now lives in the terminal read op, not a syscall
    # bare-fd branch.
    assert 'read_raw_available_blocking(out, __len, 0)' in tty
    assert 'input_mode() == TerminalInputMode::Raw' in tty


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


def test_console_api_uses_render_backend_without_serial_mirroring() -> None:
    console_h = read_source('include/bigos/console.h')
    render_h = read_source('include/bigos/console_render.h')
    console = read_source('kernel/core/terminal/console.cc')
    render = read_source('kernel/core/terminal/console_render.cc')
    vga = read_source('kernel/drivers/video/vga.cc')
    io = read_source('kernel/core/bigos/io.cc')

    assert 'void console_put(char ch) noexcept;' in console_h
    assert 'void console_write(const char *s) noexcept;' in console_h
    assert 'void console_scroll_page_up() noexcept;' in console_h
    assert 'struct ConsoleRenderBackend' in render_h
    assert 'enum class ConsoleCellRole' in render_h
    assert 'uint32_t codepoint;' in render_h
    assert 'ConsoleCellRole role;' in render_h
    assert 'struct ConsoleDisplayAttr' in render_h
    assert 'ConsoleDisplayAttr attr;' in render_h
    assert 'uint8_t visible_columns;' in render_h
    assert 'uint8_t visible_rows;' in render_h
    assert 'ConsoleRenderCell lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_RENDER_MAX_WIDTH];' in console
    assert 'CONSOLE_SCROLLBACK_LINES = 256' in console
    assert 'Utf8Decoder utf8;' in console
    assert 'console_render_backend()' in console
    assert 'bigos::device::fill_video_text_cell' not in console
    assert 'const bigos::terminal::ConsoleRenderBackend g_vga_backend' in render
    assert 'bigos::device::fill_video_text_cell' in render
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
    render_h = read_source('include/bigos/console_render.h')

    assert 'CONSOLE_RENDER_VGA_WIDTH = 80' in render_h
    assert 'CONSOLE_RENDER_VGA_HEIGHT = 25' in render_h
    assert 'CONSOLE_RENDER_MIN_WIDTH = CONSOLE_RENDER_VGA_WIDTH' in render_h
    assert 'CONSOLE_RENDER_MAX_WIDTH = 240' in render_h
    assert 'CONSOLE_RENDER_MAX_HEIGHT = 80' in render_h
    assert 'CONSOLE_SCROLLBACK_LINES = 256' in console
    assert 'uint64_t oldest_line;' in console
    assert 'uint64_t current_line;' in console
    assert 'uint64_t viewport_top;' in console
    assert 'uint8_t visible_columns;' in console
    assert 'uint8_t visible_rows;' in console
    assert 'uint8_t cursor_x;' in console
    assert 'ConsoleDisplayAttr current_attr;' in console
    assert 'uint8_t saved_cursor_x;' in console
    assert 'uint8_t saved_cursor_y;' in console
    assert 'VtParser parser;' in console
    assert 'Utf8Decoder utf8;' in console
    assert 'uint8_t visible_columns() noexcept' in console
    assert 'uint8_t visible_rows() noexcept' in console
    assert 'g_console.visible_columns = backend.visible_columns;' in console
    assert 'uint64_t bottom_viewport_top() noexcept' in console
    assert 'bool viewport_at_bottom() noexcept' in console
    assert 'void render_viewport() noexcept' in console
    assert 'backend.begin_viewport_redraw();' in console
    assert 'backend.draw_cell((uint8_t)x, (uint8_t)y, cell);' in console
    assert 'backend.set_cursor(g_console.cursor_x, (uint8_t)cursor_screen_line, true, cell);' in console
    assert 'backend.set_cursor(0, 0, false, blank);' in console
    assert 'g_console.current_line + 1 - CONSOLE_SCROLLBACK_LINES' in console
    assert 'if (follow)' in console
    assert 'console_scroll_page_up' in console
    assert 'const uint64_t step = visible_rows() > 1 ? visible_rows() - 1 : 1;' in console
    assert 'console_scroll_page_down' in console
    assert 'console_scroll_home' in console
    assert 'console_scroll_end' in console
    assert 'Clear-screen discards retained runtime scrollback' in console
    assert 'console_render_backend().clear();' in console


def test_unicode_console_text_model_decodes_utf8_and_replacement() -> None:
    console = read_source('kernel/core/terminal/console.cc')
    render_h = read_source('include/bigos/console_render.h')

    assert 'constexpr uint32_t REPLACEMENT_CODEPOINT = 0xfffd;' in console
    assert 'struct Utf8Decoder' in console
    assert 'uint32_t min_codepoint;' in console
    assert 'uint8_t remaining;' in console
    assert 'bool is_utf8_continuation(uint8_t byte) noexcept' in console
    assert 'bool valid_unicode_scalar(uint32_t codepoint) noexcept' in console
    assert 'void consume_output_byte(uint8_t byte) noexcept' in console
    assert 'byte >= 0xc2u && byte <= 0xdfu' in console
    assert 'byte >= 0xe0u && byte <= 0xefu' in console
    assert 'byte >= 0xf0u && byte <= 0xf4u' in console
    assert 'codepoint < min_codepoint || !valid_unicode_scalar(codepoint)' in console
    assert 'emit_replacement();' in console
    assert 'Replacement,' in render_h


def test_unicode_console_cell_layout_width_backspace_and_tab_stop() -> None:
    console = read_source('kernel/core/terminal/console.cc')
    render_h = read_source('include/bigos/console_render.h')

    assert 'WideLeading,' in render_h
    assert 'WideTrailing,' in render_h
    assert 'uint8_t codepoint_cell_width(uint32_t codepoint' in console
    assert 'bigos::font::lookup_glyph(codepoint, &glyph)' in console
    assert 'bigos::font::lookup_glyph(REPLACEMENT_CODEPOINT, &glyph)' in console
    assert 'write_wide_cell(uint32_t codepoint' in console
    assert 'cells[g_console.cursor_x + 1] = {codepoint, ConsoleCellRole::WideTrailing, g_console.current_attr};' in console
    assert 'if (g_console.cursor_x + 1 >= columns)' in console
    assert 'void put_backspace() noexcept' in console
    assert 'cells[g_console.cursor_x].role == ConsoleCellRole::WideTrailing' in console
    assert 'constexpr uint8_t TAB_STOP_COLUMNS = 4;' in console
    assert 'TAB_STOP_COLUMNS - (g_console.cursor_x % TAB_STOP_COLUMNS)' in console
    assert 'put_codepoint(SPACE_CODEPOINT);' in console


def test_default_user_stdout_and_stderr_reach_visible_console() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')
    tty = read_source('kernel/core/terminal/tty.cc')
    tty_h = read_source('include/bigos/tty.h')
    proc = read_source('kernel/core/proc/proc.cc')

    # The terminal is a standard vfs::File: bare fd 0/1/2 special cases are gone
    # and read/write route uniformly through the fd table -> file->ops.
    assert '(__fd == 1 || __fd == 2)' not in syscall
    assert 'bigos::proc::file_for_fd_current(0) == nullptr' not in syscall
    assert 'bigos::proc::write_fd_current((uint32_t)__fd, bounded, (size_t)__len, &bytes_written)' in syscall
    assert 'bigos::proc::read_fd_current((uint32_t)__fd, bounded, (size_t)__len, &bytes_read)' in syscall

    # The headless write marker and default console write now live inside the
    # terminal write op (device/handle layering), preserving text and order.
    assert 'serial_puts("BIGOS_USER_WRITE_SYSCALL\\n");' in tty
    assert 'serial_puts(bounded);' in tty
    assert 'default_terminal_write(bounded);' in tty
    assert '__len > bigos::sys::SYS_WRITE_MAX_LEN' in tty
    assert 'bigos::arch::vm_user::activate_kernel_address_space(process->kernel_address_space_root);' in tty
    assert 'bigos::arch::vm_user::activate_address_space(active_root);' in tty

    # The terminal read op reuses the existing blocking terminal read path,
    # selecting raw vs canonical via input_mode().
    assert 'input_mode() == TerminalInputMode::Raw' in tty
    assert 'read_raw_available_blocking(out, __len, 0)' in tty
    assert 'read_char_blocking(out, 0)' in tty

    # Device/handle split: TTY_OPS table, RDWR handle constructor, ops-pointer id.
    assert 'const vfs::FileOperations TTY_OPS = {&tty_read, &tty_close, &tty_write, &tty_lseek,' in tty
    assert 'void tty_close(vfs::File *) noexcept {}' in tty
    assert 'bigos::vfs::File *create_tty_file() noexcept;' in tty_h
    assert 'bool is_tty_file(const bigos::vfs::File *file) noexcept;' in tty_h

    # Standard fd 0/1/2 are installed as one shared terminal handle (ref_count 3)
    # at the single fresh ring3 entry point.
    assert 'bool install_standard_fds(bigos::proc::Process *__process) noexcept' in proc
    assert 'bigos::terminal::create_tty_file();' in proc
    assert 'if (!install_standard_fds(__process))' in proc


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


def test_vt_ansi_console_parser_supports_bounded_sgr_cursor_erase_and_recovery() -> None:
    console = read_source('kernel/core/terminal/console.cc')

    assert 'enum class VtParserState' in console
    assert 'Ground' in console
    assert 'Escape' in console
    assert 'Csi' in console
    assert 'CSI_MAX_PARAMS = 8' in console
    assert 'CSI_PARAM_MAX = 999u' in console
    assert 'if (byte == 0x1bu)' in console
    assert "if (byte == '[')" in console
    assert "if (byte == '7')" in console
    assert "if (byte == '8')" in console
    assert 'reset_decoder();' in console
    assert 'void consume_csi_byte(uint8_t byte) noexcept' in console
    assert 'g_console.parser.overflow = true;' in console
    assert 'reset_vt_parser();' in console
    assert 'void apply_sgr() noexcept' in console
    for token in ('param >= 30 && param <= 37', 'param >= 40 && param <= 47', 'param >= 90 && param <= 97', 'param >= 100 && param <= 107'):
        assert token in console
    assert "case 'A':" in console
    assert "case 'B':" in console
    assert "case 'C':" in console
    assert "case 'D':" in console
    assert "case 'H':" in console
    assert "case 'f':" in console
    assert "case 'J':" in console
    assert "case 'K':" in console
    assert "case 's':" in console
    assert "case 'u':" in console
    assert 'void erase_display(uint16_t mode) noexcept' in console
    assert 'void erase_current_line(uint16_t mode) noexcept' in console
    assert 'void save_cursor() noexcept' in console
    assert 'void restore_cursor() noexcept' in console
    assert 'set_cursor_visible(0, 0);' in console
