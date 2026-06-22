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


def test_pit_constants_and_programming_are_explicit() -> None:
    pit_h = read_source('include/drivers/timer/pit.h')
    pit_cc = read_source('kernel/drivers/timer/pit.cc')

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
    isr = read_source('kernel/core/irq/isr.cc')

    assert 'IRQ_LINE_TIMER = 0' in interrupt_h
    assert 'VECTOR_TIMER = I8259_MASTER_VECTOR_BASE + IRQ_LINE_TIMER' in interrupt_h

    pit_index = isr.index('bigos::device::init_pit_timer();')
    register_index = isr.index('register_isr(VECTOR_TIMER, &isr_timer, VectorOwner::Pic);')
    unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);')
    keyboard_register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Pic);')
    keyboard_unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')

    assert pit_index < register_index < unmask_index
    assert keyboard_register_index < keyboard_unmask_index


def test_timer_handler_does_not_send_pic_eoi_or_allocate() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    timer_cc = read_source('kernel/core/timer/timer.cc')
    timer_h = read_source('include/bigos/timer.h')

    handler_start = isr.index('implement_isr(timer)')
    handler_end = isr.index('implement_isr(keyboard)')
    handler_body = isr[handler_start:handler_end]

    # Tick is advanced through the controlled timer-owned API, not by mutating
    # timer-internal tick storage from the IRQ layer.
    assert 'bigos::timer::on_tick();' in handler_body
    assert '++bigos::timer::__detail::g_ticks' not in handler_body
    assert '++bigos::timer::__detail::g_ticks' not in isr

    # Tick state ownership lives in the timer translation unit.
    assert 'void on_tick() noexcept;' in timer_h
    assert 'volatile tick_t g_ticks = 0;' in timer_cc
    assert 'volatile tick_t g_ticks = 0;' not in isr
    assert '++__detail::g_ticks;' in timer_cc

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
        'mdelay',
        'while (ticks()',
    )
    for token in forbidden_tokens:
        assert token not in handler_body


def test_mdelay_and_tick_polling_not_in_any_isr_handler_body() -> None:
    isr = read_source('kernel/core/irq/isr.cc')

    timer_start = isr.index('implement_isr(timer)')
    keyboard_start = isr.index('implement_isr(keyboard)')
    init_start = isr.index('void init_isr_timer()')

    timer_body = isr[timer_start:keyboard_start]
    keyboard_body = isr[keyboard_start:init_start]

    for body in (timer_body, keyboard_body):
        assert 'mdelay' not in body
        assert 'while (ticks()' not in body
        assert 'while(ticks()' not in body


def test_timer_self_test_and_irq_enable_boundaries() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    isr = read_source('kernel/core/irq/isr.cc')

    self_test_index = kernel.index('bigos::mm::self_test();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    timer_init_index = isr.index('bigos::device::init_pit_timer();')

    assert self_test_index < init_irq_index < enable_irq_index
    assert timer_init_index < isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);')


def test_timer_smoke_is_default_off_gated_and_bounded() -> None:
    xmake = read_source('xmake.lua')
    # timer = read_source('kernel/core/timer/timer.cc')

    option_index = xmake.index('option("timer_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_TIMER_SMOKE")')

    assert option_index < default_index < define_index
    assert 'driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);' in read_source('kernel/core/irq/isr.cc')
    isr = read_source('kernel/core/irq/isr.cc')
    assert '#ifdef BIGOS_TIMER_SMOKE' in isr
    assert 'TIMER_SMOKE_MARKER_LIMIT = 3' in isr
    assert 'if (bigos::timer::ticks() <= TIMER_SMOKE_MARKER_LIMIT)' in isr
    assert 'serial_puts("BIGOS_TIMER_IRQ\\n");' in isr


def test_timer_delay_api_documents_busy_wait_semantics() -> None:
    timer_h = read_source('include/bigos/timer.h')
    timer_cc = read_source('kernel/core/timer/timer.cc')
    docs = read_source('docs/en/arch/timer-irq-foundation.md')

    assert 'void mdelay(uint64_t milliseconds) noexcept;' in timer_h
    assert 'Busy-waits on the early PIT tick counter' in timer_h
    assert 'not scheduler sleep' in docs
    assert 'while (ticks() < target)' in timer_cc
    assert 'asm volatile("pause")' in timer_cc


def test_timer_api_context_contracts_are_documented() -> None:
    timer_h = read_source('include/bigos/timer.h')

    assert 'IRQ-context only.' in timer_h
    assert 'Context-agnostic read.' in timer_h
    assert 'Non-interrupt context only.' in timer_h
    assert 'busy-waits forever' in timer_h


def test_isr_abi_invariants_are_preserved() -> None:
    interrupt_s = read_source('kernel/core/irq/interrupt.s')
    interrupt_cc = read_source('kernel/core/irq/interrupt.cc')

    common_start = interrupt_s.index('isr_common:')
    common_end = interrupt_s.index('.data', common_start)
    common_body = interrupt_s[common_start:common_end]

    # General-purpose registers saved in reverse InterruptFrame order (r15..rax in
    # memory), restored in mirrored order, returning through iretq.
    save_order = [
        'pushq %rbx',
        'pushq %rcx',
        'pushq %rdx',
        'pushq %rbp',
        'pushq %rsi',
        'pushq %rdi',
        'pushq %r8',
        'pushq %r9',
        'pushq %r10',
        'pushq %r11',
        'pushq %r12',
        'pushq %r13',
        'pushq %r14',
        'pushq %r15',
    ]
    indices = [common_body.index(token) for token in save_order]
    assert indices == sorted(indices)
    assert 'popq %r15' in common_body
    assert 'iretq' in common_body

    # 16-byte stack alignment before the C++ dispatch call.
    align_index = common_body.index('andq $-16, %rsp')
    call_index = common_body.index('call irq_dispatch')
    assert align_index < call_index

    # Synthetic zero error-code slot keeps the frame layout stable for vectors
    # without a CPU-provided error code.
    assert 'pushq $0' in interrupt_s
    assert '.if \\has_ecode == 0' in interrupt_s

    # External IRQ sends exactly one EOI after the handler returns; CPU
    # exceptions send none.
    exception_start = interrupt_cc.index('is_cpu_exception(__frame->vector)')
    exception_end = interrupt_cc.index('is_pic_external_irq(__frame->vector)')
    exception_block = interrupt_cc[exception_start:exception_end]
    assert 'send_eoi' not in exception_block
    assert interrupt_cc.count('driver::irqchip::i8259::send_eoi') == 1
