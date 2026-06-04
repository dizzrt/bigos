from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_exception_dispatch_does_not_send_pic_eoi() -> None:
    interrupt_s = read_source('src/kernel/irq/interrupt.s')
    interrupt_cc = read_source('src/kernel/irq/interrupt.cc')

    assert 'outb' not in interrupt_s
    assert 'I8259_EOI' not in interrupt_s

    exception_start = interrupt_cc.index('if (__detail::is_cpu_exception(__frame->vector))')
    external_start = interrupt_cc.index('if (__detail::is_i8259_external_irq(__frame->vector))')
    exception_body = interrupt_cc[exception_start:external_start]

    assert 'send_eoi' not in exception_body
    assert 'driver::irqchip::i8259::send_eoi' in interrupt_cc[external_start:]


def test_keyboard_irq_handler_is_registered_before_unmask() -> None:
    isr = read_source('src/kernel/irq/isr.cc')

    register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard);')
    unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')

    assert register_index < unmask_index
    assert 'PS2_KEYBOARD_DATA_PORT = 0x60' in isr
    assert 'BIGOS_KEYBOARD_IRQ scancode=' in isr


def test_memory_self_test_stays_before_irq_pic_and_enable() -> None:
    kernel = read_source('src/kernel/kernel.cc')

    init_mem_index = kernel.index('bigos::init_mem(boot_info);')
    self_test_index = kernel.index('bigos::mm::self_test();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')

    assert init_mem_index < self_test_index < init_irq_index < enable_irq_index


def test_page_fault_handler_is_diagnostic_only() -> None:
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    handler_start = interrupt.index('static void page_fault_handler')
    handler_end = interrupt.index('static void default_external_irq_handler')
    handler_body = interrupt[handler_start:handler_end]

    assert 'read_cr2()' in handler_body
    assert 'BIGOS_PAGE_FAULT' in handler_body
    assert 'halt_cpu();' in handler_body

    forbidden_tokens = (
        'alloc_kernel_pages',
        'alloc_physical_order',
        'kmalloc',
        'map_kernel_range',
        'set_paging',
        'retry',
    )
    for token in forbidden_tokens:
        assert token not in handler_body


def test_kernel_runtime_idt_uses_static_storage_and_lidt() -> None:
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    assert 'INTRDescriptor kernel_idt[IRQ_COUNT];' in interrupt
    assert 'INTRDescriptor *idt = (INTRDescriptor *)IDT_BASE' not in interrupt
    assert 'asm volatile("lidt (%0)"' in interrupt
    assert 'PRESENT_RING0_INTERRUPT_GATE = 0x8e00' in interrupt
