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


def test_exception_dispatch_does_not_send_pic_eoi() -> None:
    interrupt_s = read_source('kernel/core/irq/interrupt.s')
    interrupt_cc = read_source('kernel/core/irq/interrupt.cc')

    assert 'outb' not in interrupt_s
    assert 'I8259_EOI' not in interrupt_s

    exception_start = interrupt_cc.index('if (__detail::is_cpu_exception(__frame->vector))')
    external_start = interrupt_cc.index('if (__detail::is_i8259_external_irq(__frame->vector))')
    exception_body = interrupt_cc[exception_start:external_start]

    assert 'send_eoi' not in exception_body
    assert 'driver::irqchip::i8259::send_eoi' in interrupt_cc[external_start:]


def test_i8259_spurious_irq7_is_checked_before_default_handler() -> None:
    pic_h = read_source('include/drivers/irqchip/i8259.h')
    pic = read_source('kernel/drivers/irqchip/i8259.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert 'bool is_spurious_irq(uint8_t __irq) noexcept;' in pic_h
    assert 'void acknowledge_spurious_irq(uint8_t __irq) noexcept;' in pic_h
    assert 'OCW3_READ_ISR = 0x0b' in pic
    assert 'read_isr(slave)' in pic
    assert '__irq != I8259_IRQ_LPTA && __irq != 15' in pic

    spurious_index = interrupt.index('driver::irqchip::i8259::is_spurious_irq(irq_line)')
    handler_index = interrupt.index('IRQHandler handler = __detail::isr_list[__frame->vector];')
    eoi_index = interrupt.index('driver::irqchip::i8259::send_eoi(irq_line);')

    assert spurious_index < handler_index < eoi_index


def test_irq_return_preemption_runs_after_nonblocking_guard_scope() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    external_start = interrupt.index('if (__detail::is_i8259_external_irq(__frame->vector))')
    syscall_start = interrupt.index('if (__detail::is_syscall_vector(__frame->vector))')
    external_body = interrupt[external_start:syscall_start]

    guard_index = external_body.index('__detail::NonblockingContextGuard nonblocking_guard;')
    eoi_index = external_body.index('driver::irqchip::i8259::send_eoi(irq_line);')
    guard_close_index = external_body.index('}\n            bigos::sched::maybe_preempt_on_irq_return(__frame);')
    preempt_index = external_body.index('bigos::sched::maybe_preempt_on_irq_return(__frame);')

    assert guard_index < eoi_index < guard_close_index < preempt_index


def test_keyboard_irq_handler_is_registered_before_unmask() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    xmake = read_source('xmake.lua')

    register_index = isr.index('register_isr(VECTOR_KEYBOARD, &isr_keyboard);')
    unmask_index = isr.index('driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);')

    # Stage 19: keyboard IRQ1 is unmasked unconditionally (no BIGOS_KEYBOARD_SMOKE
    # guard in isr.cc) so the default-boot interactive /bin/sh can read the TTY.
    assert '#ifdef BIGOS_KEYBOARD_SMOKE' not in isr
    assert register_index < unmask_index
    assert 'PS2_KEYBOARD_DATA_PORT = 0x60' in isr
    assert 'bigos::input::handle_keyboard_scancode(scancode);' in isr
    assert 'option("keyboard_smoke")' in xmake
    assert 'set_default(false)' in xmake[xmake.index('option("keyboard_smoke")') :]
    assert 'add_defines("BIGOS_KEYBOARD_SMOKE")' in xmake


def test_memory_self_test_stays_before_irq_pic_and_enable() -> None:
    kernel = read_source('kernel/core/kernel.cc')

    serial_init_index = kernel.index('bigos::serial_init();')
    init_mem_index = kernel.index('bigos::init_mem(boot_info);')
    self_test_index = kernel.index('bigos::mm::self_test();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    serial_marker_index = kernel.index('bigos::serial_puts("BigOS kernel reached\\n");')

    assert serial_init_index < init_mem_index < self_test_index < init_irq_index < enable_irq_index
    assert serial_init_index < serial_marker_index


def test_page_fault_handler_is_diagnostic_only() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    handler_start = interrupt.index('static bool page_fault_handler')
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
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert 'INTRDescriptor kernel_idt[IRQ_COUNT];' in interrupt
    assert 'INTRDescriptor *idt = (INTRDescriptor *)IDT_BASE' not in interrupt
    assert 'asm volatile("lidt (%0)"' in interrupt
    assert 'PRESENT_RING0_INTERRUPT_GATE = 0x8e00' in interrupt


def test_isr_common_aligns_stack_before_cpp_dispatch() -> None:
    interrupt_s = read_source('kernel/core/irq/interrupt.s')

    frame_index = interrupt_s.index('movq %rsp, %rdi')
    save_index = interrupt_s.index('movq %rsp, %rax', frame_index)
    align_index = interrupt_s.index('andq $-16, %rsp', save_index)
    pad_index = interrupt_s.index('subq $8, %rsp', align_index)
    push_index = interrupt_s.index('pushq %rax', pad_index)
    call_index = interrupt_s.index('call irq_dispatch', push_index)
    restore_index = interrupt_s.index('movq (%rsp), %rsp', call_index)
    pop_index = interrupt_s.index('popq %r15', restore_index)

    assert frame_index < save_index < align_index < pad_index < push_index < call_index < restore_index < pop_index


def test_isr_common_preserves_interrupted_rax_before_using_scratch() -> None:
    interrupt_s = read_source('kernel/core/irq/interrupt.s')

    common_start = interrupt_s.index('isr_common:')
    save_rax_index = interrupt_s.index('pushq %rax', common_start)
    synth_rsp_index = interrupt_s.index('leaq 64(%rsp), %rax', save_rax_index)
    fill_rsp_index = interrupt_s.index('movq %rax, 8(%rsp)', synth_rsp_index)
    save_rbx_index = interrupt_s.index('pushq %rbx', fill_rsp_index)

    assert 'movq %rsp, %rax\n    addq $40, %rax' not in interrupt_s
    assert save_rax_index < synth_rsp_index < fill_rsp_index < save_rbx_index
