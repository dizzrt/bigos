from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_vector_ownership_table_drives_eoi_selection() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    isr = read_source('kernel/core/irq/isr.cc')

    assert 'enum class VectorOwner' in interrupt_h
    assert 'CpuException,' in interrupt_h
    assert 'Syscall,' in interrupt_h
    assert 'Pic,' in interrupt_h
    assert 'Lapic,' in interrupt_h
    assert 'VectorOwner vector_owners[IRQ_COUNT];' in interrupt

    pic_branch = interrupt[
        interrupt.index('if (__detail::is_pic_external_irq(__frame->vector))') :
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))')
    ]
    lapic_branch = interrupt[
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))') :
        interrupt.index('if (__detail::is_apic_spurious_vector(__frame->vector))')
    ]
    syscall_branch = interrupt[
        interrupt.index('if (__detail::is_syscall_vector(__frame->vector))') :
        interrupt.index('__detail::unknown_vector_handler(__frame);')
    ]

    assert 'driver::irqchip::i8259::send_eoi(irq_line);' in pic_branch
    assert 'driver::irqchip::lapic::send_eoi();' not in pic_branch
    assert 'driver::irqchip::lapic::send_eoi();' in lapic_branch
    assert 'driver::irqchip::i8259::send_eoi' not in lapic_branch
    assert 'send_eoi' not in syscall_branch

    assert 'register_isr(VECTOR_LAPIC_TIMER, &isr_timer, VectorOwner::Lapic)' in isr
    assert 'register_isr(VECTOR_TIMER, &isr_timer, VectorOwner::Pic)' in isr
    assert 'register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Lapic)' in isr
    assert 'register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Pic)' in isr


def test_ioapic_keyboard_route_is_targeted_and_falls_back_before_ap_start() -> None:
    ioapic_h = read_source('include/drivers/irqchip/ioapic.h')
    ioapic = read_source('kernel/drivers/irqchip/ioapic.cc')
    isr = read_source('kernel/core/irq/isr.cc')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'struct RedirectionConfig' in ioapic_h
    assert 'TriggerMode::Edge' in isr
    assert 'Polarity::ActiveHigh' in isr
    assert 'destination_apic_id' in ioapic_h
    assert 'REDIR_POLARITY_LOW' in ioapic
    assert 'REDIR_TRIGGER_LEVEL' in ioapic

    target_start = isr.index('bool select_default_irq_target')
    target_end = isr.index('void init_pic_timer_fallback')
    target_body = isr[target_start:target_end]
    assert 'BOOTSTRAP_CPU_ID' in target_body
    assert 'CpuStartupState::Online' in target_body
    assert 'LocalTimerState::Ready' in target_body
    assert 'INVALID_APIC_ID' in target_body

    assert 'driver::irqchip::ioapic::init(ioapic.address)' in isr
    assert 'driver::irqchip::ioapic::route_irq(config)' in isr
    assert 'driver::irqchip::i8259::disable_irq(IRQ_LINE_KEYBOARD);' in isr
    assert 'BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE' in isr
    assert 'BIGOS_APIC_DEFAULT_FALLBACK_PIC_PIT' in isr

    topology_index = kernel.index('bigos::cpu::init_topology_from_mp();')
    irq_index = kernel.index('bigos::irq::initIRQ();')
    ap_gate_index = kernel.index('if (bigos::irq::apic_default_delivery_active())')
    startup_index = kernel.index('start_application_processors();')
    fallback_index = kernel.index('BIGOS_APIC_DEFAULT_BSP_ONLY_FALLBACK')
    assert topology_index < irq_index < ap_gate_index < startup_index < fallback_index


def test_unknown_and_spurious_vectors_have_deterministic_owner_diagnostics() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert 'vector_owners[0xff] = VectorOwner::ApicSpurious;' in interrupt
    assert 'BIGOS_APIC_SPURIOUS vector=%x owner=%s' in interrupt
    assert 'BIGOS_UNKNOWN_VECTOR vector=%x owner=%s' in interrupt
    assert 'vector_owner_name(VectorOwner __owner)' in interrupt
    assert 'return "pic";' in interrupt
    assert 'return "lapic";' in interrupt
    assert 'return "unknown";' in interrupt


def test_apic_default_hard_irq_paths_remain_nonblocking() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    timer_body = isr[isr.index('implement_isr(timer)') : isr.index('implement_isr(keyboard)')]
    keyboard_body = isr[isr.index('implement_isr(keyboard)') : isr.index('implement_isr(scheduler_nudge)')]
    lapic_branch = interrupt[
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))') :
        interrupt.index('if (__detail::is_apic_spurious_vector(__frame->vector))')
    ]

    for body in (timer_body, keyboard_body, lapic_branch):
        for token in ('kmalloc', 'alloc_kernel_pages', 'sleep_for', 'wait_queue_wait', 'fs::', 'block::'):
            assert token not in body

    assert 'NonblockingContextGuard nonblocking_guard;' in lapic_branch
    assert 'handler(__frame);' in lapic_branch
    assert 'driver::irqchip::lapic::send_eoi();' in lapic_branch
