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


def test_msix_capability_parse_and_control_are_bounded() -> None:
    config_h = read_source('include/drivers/pci/config.h')
    msix_h = read_source('include/drivers/pci/msix.h')
    msix = read_source('kernel/drivers/pci/msix.cc')

    assert 'Status write_config16(FunctionAddress __address, uint8_t __offset, uint16_t __value)' in config_h
    assert 'CAPABILITY_ID = 0x11' in msix_h
    assert 'MAX_TABLE_ENTRIES = 2048' in msix_h
    assert 'struct CapabilityInfo' in msix_h
    assert 'table_bar_index' in msix_h
    assert 'pba_bar_index' in msix_h
    assert 'function_masked' in msix_h

    parse_body = msix[msix.index('Status find_capability') : msix.index('Status refresh_control')]
    assert 'context_allows_msix_config()' in parse_body
    assert 'pci::read_capabilities' in parse_body
    assert 'caps[i].id != CAPABILITY_ID' in parse_body
    assert 'MESSAGE_CONTROL_TABLE_SIZE_MASK' in parse_body
    assert 'MESSAGE_CONTROL_FUNCTION_MASK' in parse_body
    assert 'MESSAGE_CONTROL_ENABLE' in parse_body
    assert 'table & BIR_MASK' in parse_body
    assert 'pba & BIR_MASK' in parse_body
    assert 'table & OFFSET_MASK' in parse_body
    assert 'pba & OFFSET_MASK' in parse_body
    assert 'return Status::Unsupported;' in parse_body

    assert 'set_function_mask(const CapabilityInfo &__info, bool __masked)' in msix
    assert 'set_enable(const CapabilityInfo &__info, bool __enabled)' in msix
    assert 'pci::write_config16' in msix
    assert 'local.irq_nesting_depth == 0' in msix
    assert 'local.nonblocking_depth == 0' in msix
    assert 'local.preemption_disable_depth == 0' in msix


def test_msix_table_mapping_and_programming_use_uncached_mask_first_order() -> None:
    memory_h = read_source('include/bigos/memory.h')
    vmem = read_source('kernel/mm/vmem.cc')
    msix = read_source('kernel/drivers/pci/msix.cc')

    assert 'DEVICE_UNCACHED' in memory_h
    assert 'WRITE_THROUGH' in memory_h
    assert 'CACHE_DISABLE' in memory_h
    assert 'DeviceMmioCachePolicy::Uncached' in msix
    assert 'page_attr::DEVICE_UNCACHED' in vmem

    map_body = msix[msix.index('Status map_table_and_pba') : msix.index('Status set_entry_mask')]
    assert 'pci::read_bar(__info.address, __info.table_bar_index' in map_body
    assert 'pci::read_bar(__info.address, __info.pba_bar_index' in map_body
    assert 'range_within_bar(table_bar' in map_body
    assert 'range_within_bar(pba_bar' in map_body
    assert 'bigos::mm::map_device_mmio' in map_body
    assert 'return Status::MappingFailed;' in map_body

    program_body = msix[msix.index('Status program_entry') : msix.index('Status allocate_and_program_entry')]
    mask_first = program_body.index('set_entry_mask(__mapping, __entry_index, true)')
    addr_high = program_body.index('entry.message_address_high')
    addr_low = program_body.index('entry.message_address_low')
    data = program_body.index('entry.message_data = __vector')
    unmask = program_body.index('set_entry_mask(__mapping, __entry_index, false)')
    assert mask_first < addr_high < addr_low < data < unmask
    assert 'X86_MSI_ADDRESS_BASE' in program_body
    assert 'X86_MSI_DESTINATION_SHIFT' in program_body


def test_msix_vectors_reuse_lapic_owned_dispatch_and_default_off_smoke() -> None:
    msix = read_source('kernel/drivers/pci/msix.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

    alloc_body = msix[msix.index('Status allocate_and_program_entry') : msix.index('const char *status_name')]
    assert 'bigos::irq::allocate_lapic_vector(__handler, &vector)' in alloc_body
    assert 'program_entry(__mapping, __entry_index, vector, __target_apic_id, true)' in alloc_body
    assert 'bigos::irq::release_lapic_vector(vector)' in alloc_body

    lapic_branch = interrupt[
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))') : interrupt.index(
            'if (__detail::is_apic_spurious_vector(__frame->vector))'
        )
    ]
    assert 'driver::irqchip::lapic::send_eoi();' in lapic_branch
    assert 'driver::irqchip::i8259::send_eoi' not in lapic_branch

    assert 'option("pci_msix_smoke")' in xmake
    option_index = xmake.index('option("pci_msix_smoke")')
    assert 'set_default(false)' in xmake[option_index:]
    assert 'add_defines("BIGOS_PCI_MSIX_SMOKE")' in xmake
    assert 'add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")' in xmake[xmake.index('if has_config("pci_msix_smoke")') :]
    assert '#ifdef BIGOS_PCI_MSIX_SMOKE' in kernel
    assert 'driver::pci::msix::smoke();' in kernel
    assert 'BIGOS_PCI_MSIX_DELIVERY_PASSED' in msix
    assert 'BIGOS_PCI_MSIX_PASSED' in msix
    assert 'BIGOS_PCI_MSIX_SMOKE_SKIPPED lapic' in msix


def test_msix_smoke_keeps_irq_path_allocation_free_and_uses_controlled_producer() -> None:
    msix = read_source('kernel/drivers/pci/msix.cc')

    handler_body = msix[msix.index('void smoke_irq_handler') : msix.index('bool scan_for_msix_capability')]
    for token in ('kmalloc', 'alloc_kernel_pages', 'map_device_mmio', 'read_bar', 'read_capabilities', 'sleep_for'):
        assert token not in handler_body

    trigger_body = msix[msix.index('bool controlled_trigger') : msix.index('bool wait_for_handler')]
    assert 'entry.vector_control & VECTOR_CONTROL_MASKED' in trigger_body
    assert 'driver::irqchip::lapic::send_fixed_ipi' in trigger_body

    smoke_body = msix[msix.index('bool smoke() noexcept') :]
    assert 'find_capability({0xff, 31, 7}, &absent) != Status::NoDevice' in smoke_body
    assert 'scan_for_msix_capability()' in smoke_body
    assert 'set_entry_mask(mapping, 0, false)' in smoke_body
    assert 'set_entry_mask(mapping, 0, true)' in smoke_body
    assert 'mapping.table[0].message_data != programmed.vector' in smoke_body
