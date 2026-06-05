from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_pit_constants_and_programming_are_explicit() -> None:
    pit_h = read_source('include/drivers/timer/pit.h')
    pit_cc = read_source('src/drivers/timer/pit.cc')

    assert 'CHANNEL0_DATA_PORT = 0x40' in pit_h
    assert 'COMMAND_PORT = 0x43' in pit_h
    assert 'BASE_FREQUENCY_HZ = 1193182' in pit_h
    assert 'TIMER_HZ = 100' in pit_h
    assert 'CHANNEL0_DIVISOR = (uint16_t)(BASE_FREQUENCY_HZ / TIMER_HZ)' in pit_h
    assert 'CHANNEL0_RATE_COMMAND = 0x36' in pit_h

    command_index = pit_cc.index('bigos::outb(COMMAND_PORT, CHANNEL0_RATE_COMMAND);')
    low_index = pit_cc.index('bigos::outb(CHANNEL0_DATA_PORT, (uint8_t)(CHANNEL0_DIVISOR & 0xff));')
    high_index = pit_cc.index('bigos::outb(CHANNEL0_DATA_PORT, (uint8_t)((CHANNEL0_DIVISOR >> 8) & 0xff));')

    assert command_index < low_index < high_index


def test_timer_irq0_handler_is_registered_before_unmask() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')
    isr = read_source('src/kernel/irq/isr.cc')

    assert 'IRQ_LINE_TIMER = 0' in interrupt_h
    assert 'VECTOR_TIMER = I8259_MASTER_VECTOR_BASE + IRQ_LINE_TIMER' in interrupt_h

    pit_index = isr.index('driver::timer::pit::init_channel0();')
    register_index = isr.index('register_isr(VECTOR_TIMER, &isr_timer);')
    unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);')
    keyboard_register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard);')
    keyboard_unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')

    assert pit_index < register_index < unmask_index
    assert keyboard_register_index < keyboard_unmask_index


def test_timer_handler_does_not_send_pic_eoi_or_allocate() -> None:
    isr = read_source('src/kernel/irq/isr.cc')
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    handler_start = isr.index('implement_isr(timer)')
    handler_end = isr.index('implement_isr(keyboard)')
    handler_body = isr[handler_start:handler_end]

    assert '++bigos::timer::__detail::g_ticks;' in handler_body
    assert 'bigos::timer::on_tick();' not in handler_body
    assert 'send_eoi' not in handler_body
    assert 'driver::irqchip::i8259::send_eoi' in interrupt

    forbidden_tokens = (
        'kmalloc',
        'alloc_kernel_pages',
        'free(',
        'filesystem',
        'scheduler',
        'sleep',
        'kprintf',
    )
    for token in forbidden_tokens:
        assert token not in handler_body


def test_timer_self_test_and_irq_enable_boundaries() -> None:
    kernel = read_source('src/kernel/kernel.cc')
    isr = read_source('src/kernel/irq/isr.cc')

    self_test_index = kernel.index('bigos::mm::self_test();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    timer_init_index = isr.index('driver::timer::pit::init_channel0();')

    assert self_test_index < init_irq_index < enable_irq_index
    assert timer_init_index < isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);')


def test_timer_smoke_is_default_off_gated_and_bounded() -> None:
    xmake = read_source('xmake.lua')
    # timer = read_source('src/kernel/timer/timer.cc')

    option_index = xmake.index('option("timer_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_TIMER_SMOKE")')

    assert option_index < default_index < define_index
    assert 'driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);' in read_source('src/kernel/irq/isr.cc')
    isr = read_source('src/kernel/irq/isr.cc')
    assert '#ifdef BIGOS_TIMER_SMOKE' in isr
    assert 'TIMER_SMOKE_MARKER_LIMIT = 3' in isr
    assert 'if (bigos::timer::__detail::g_ticks <= TIMER_SMOKE_MARKER_LIMIT)' in isr
    assert 'serial_puts("BIGOS_TIMER_IRQ\\n");' in isr


def test_timer_delay_api_documents_busy_wait_semantics() -> None:
    timer_h = read_source('include/bigos/timer.h')
    timer_cc = read_source('src/kernel/timer/timer.cc')
    docs = read_source('docs/arch/timer-irq-foundation.md')

    assert 'void mdelay(uint64_t milliseconds) noexcept;' in timer_h
    assert 'Busy-waits on the early PIT tick counter' in timer_h
    assert 'not scheduler sleep' in docs
    assert 'while (ticks() < target)' in timer_cc
    assert 'asm volatile("pause")' in timer_cc
