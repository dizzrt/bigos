from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/kernel.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def test_pci_config_access_is_port_based_and_bounded() -> None:
    header = read_source('include/drivers/pci/config.h')
    source = read_source('kernel/drivers/pci/config.cc')
    io_h = read_source('include/bigos/io.h')
    io_cc = read_source('kernel/core/bigos/io.cc')

    assert 'uint32_t inl(uint16_t port);' in io_h
    assert 'void outl(uint16_t port, uint32_t value);' in io_h
    assert 'asm volatile("inl %w1,%0"' in io_cc
    assert 'asm volatile("outl %0,%w1"' in io_cc

    assert 'CONFIG_ADDRESS_PORT = 0xcf8' in source
    assert 'CONFIG_DATA_PORT = 0xcfc' in source
    assert 'CONFIG_ENABLE = 0x80000000u' in source
    assert 'read_config32(FunctionAddress __address, uint8_t __offset, uint32_t *__out)' in source
    assert 'read_config16(FunctionAddress __address, uint8_t __offset, uint16_t *__out)' in source
    assert 'read_config8(FunctionAddress __address, uint8_t __offset, uint8_t *__out)' in source
    assert 'valid_dword_offset' in source
    assert '__offset & 0xfcu' in source

    assert 'struct FunctionAddress' in header
    assert 'enum class Status' in header
    assert 'UnsupportedContext' in header
    assert 'NoDevice' in header
    assert 'BadCapabilityList' in header
    assert 'context_allows_config_access()' in header
    assert 'local.irq_nesting_depth == 0' in source
    assert 'local.nonblocking_depth == 0' in source
    assert 'local.preemption_disable_depth == 0' in source


def test_pci_probe_capability_and_bar_contracts_are_defensive() -> None:
    header = read_source('include/drivers/pci/config.h')
    source = read_source('kernel/drivers/pci/config.cc')

    assert 'INVALID_VENDOR_ID = 0xffff' in header
    assert 'probe_device(FunctionAddress __address, DeviceId *__out)' in source
    assert 'return Status::NoDevice;' in source
    assert 'STATUS_CAPABILITIES_LIST' in source

    cap_body = source[source.index('Status read_capabilities') : source.index('Status read_bar')]
    assert 'MAX_CAPABILITIES' in cap_body
    assert 'cap_pointer_valid' in cap_body
    assert 'visited_capability' in cap_body
    assert 'return Status::BadCapabilityList;' in cap_body
    assert 'ptr &= 0xfcu;' in cap_body

    bar_body = source[source.index('Status read_bar') : source.index('const char *status_name')]
    assert 'REG_BAR0 + __bar_index * 4' in bar_body
    assert 'write_config32(__address, offset, 0xffffffffu)' in bar_body
    assert 'write_config32(__address, offset, original_low)' in bar_body
    assert 'BarKind::Io' in bar_body
    assert 'BarKind::Memory32' in bar_body
    assert 'BarKind::Memory64' in bar_body
    assert 'prefetchable' in bar_body
    assert 'decode_bar_size' in bar_body


def test_dynamic_lapic_vector_pool_avoids_fixed_vectors_and_uses_existing_eoi_path() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert 'DYNAMIC_LAPIC_VECTOR_FIRST = 0xd0' in interrupt_h
    assert 'DYNAMIC_LAPIC_VECTOR_LAST = 0xdf' in interrupt_h
    assert 'VECTOR_SYSCALL = 0x80' in interrupt_h
    assert 'VECTOR_TLB_SHOOTDOWN = 0xed' in interrupt_h
    assert 'static_assert(DYNAMIC_LAPIC_VECTOR_FIRST > I8259_VECTOR_LAST);' in interrupt
    assert 'static_assert(DYNAMIC_LAPIC_VECTOR_LAST < VECTOR_TLB_SHOOTDOWN);' in interrupt

    assert 'bool dynamic_vector_allocated[DYNAMIC_LAPIC_VECTOR_COUNT];' in interrupt
    assert 'context_allows_vector_allocation()' in interrupt_h
    assert 'allocate_lapic_vector(IRQHandler __handler, uint8_t *__vector)' in interrupt
    assert 'release_lapic_vector(uint8_t __vector)' in interrupt
    assert 'irq::isr::register_isr(vector, __handler, VectorOwner::Lapic);' in interrupt
    assert '__detail::setVectorOwner(__vector, VectorOwner::Unknown);' in interrupt
    assert 'VectorAllocStatus::Exhausted' in interrupt
    assert 'VectorAllocStatus::NotAllocated' in interrupt

    lapic_branch = interrupt[
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))') : interrupt.index(
            'if (__detail::is_apic_spurious_vector(__frame->vector))'
        )
    ]
    assert 'driver::irqchip::lapic::send_eoi();' in lapic_branch
    assert 'driver::irqchip::i8259::send_eoi' not in lapic_branch


def test_smoke_wiring_is_default_off_and_records_markers() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'option("pci_config_vector_smoke")' in xmake
    assert 'add_defines("BIGOS_PCI_CONFIG_VECTOR_SMOKE")' in xmake
    assert '#ifdef BIGOS_PCI_CONFIG_VECTOR_SMOKE' in kernel
    assert 'pci_config_vector_smoke();' in kernel
    assert 'BIGOS_PCI_CONFIG_SMOKE_PASSED' in kernel
    assert 'BIGOS_VECTOR_ALLOC_SMOKE_PASSED' in kernel
    assert 'BIGOS_PCI_CONFIG_VECTOR_PASSED' in kernel
    assert 'BIGOS_PCI_CONFIG_VECTOR_FAILED' in kernel
