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


def test_virtio_blk_is_modern_only_and_uses_pci_capabilities() -> None:
    header = read_source('include/drivers/block/virtio_blk.h')
    source = read_source('kernel/drivers/block/virtio_blk.cc')

    assert 'VIRTIO_PCI_MODERN_BLK_DEVICE_ID = 0x1042' in source
    assert 'VIRTIO_PCI_TRANSITIONAL_BLK_DEVICE_ID = 0x1001' in source
    assert 'VIRTIO_PCI_CAPABILITY_ID = 0x09' in source
    assert 'VIRTIO_PCI_CAP_COMMON_CFG' in source
    assert 'VIRTIO_PCI_CAP_NOTIFY_CFG' in source
    assert 'VIRTIO_PCI_CAP_ISR_CFG' in source
    assert 'VIRTIO_PCI_CAP_DEVICE_CFG' in source
    assert 'parse_virtio_caps' in source
    assert 'driver::pci::read_capabilities' in source
    assert 'driver::pci::read_bar' in source
    assert 'map_device_mmio' in source
    assert 'DeviceMmioCachePolicy::Uncached' in source
    assert 'VIRTIO_F_VERSION_1_BIT = 32' in source
    assert 'VIRTIO_STATUS_FEATURES_OK' in source
    assert 'VIRTIO_STATUS_DRIVER_OK' in source
    assert 'set_failed(__device)' in source
    assert 'VIRTIO_PCI_TRANSITIONAL_BLK_DEVICE_ID' in source
    assert 'parse_virtio_caps(__device->pci_address, &caps)' in source


def test_virtqueue_uses_bounded_physical_dma_and_request_tokens() -> None:
    header = read_source('include/drivers/block/virtio_blk.h')
    source = read_source('kernel/drivers/block/virtio_blk.cc')

    assert 'VIRTIO_BLK_QUEUE_SIZE = 32' in header
    assert 'VIRTIO_BLK_REQUEST_SLOTS = bigos::block_io::QUEUE_CAPACITY_PER_DEVICE' in header
    assert 'struct VirtqDesc' in header
    assert 'struct VirtqAvail' in header
    assert 'struct VirtqUsed' in header
    assert 'CompletionToken token;' in header
    assert 'alloc_physical_order(VIRTIO_QUEUE_PAGE_ORDER, 0)' in source
    assert 'alloc_physical_order(0, 0)' in source
    assert 'phys_to_direct' in source
    assert 'queue_desc = __device->queue_phys' in source
    assert 'queue_driver = __device->queue_phys +' in source
    assert 'queue_device = __device->queue_phys +' in source
    assert 'entry.token = *__token' in source
    assert 'dev->desc[head]' in source
    assert 'dev->avail->ring[dev->avail_idx % dev->queue_size] = head' in source
    assert '*dev->notify = 0' in source
    assert '__request->sector_count != 1' in source


def test_msix_completion_path_is_irq_safe_and_lapic_owned() -> None:
    source = read_source('kernel/drivers/block/virtio_blk.cc')
    msix = read_source('kernel/drivers/pci/msix.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert 'driver::pci::msix::find_capability' in source
    assert 'driver::pci::msix::map_table_and_pba' in source
    assert 'driver::pci::msix::allocate_and_program_entry' in source
    assert 'cfg->queue_msix_vector = __device->completion_vector.entry_index' in source
    assert 'set_enable(__device->msix_info, true)' in source
    assert 'set_entry_mask(' in source

    irq_body = source[source.index('void virtio_blk_irq') : source.index('const char *virtio_blk_status_name')]
    for token in ('kmalloc', 'alloc_kernel_pages', 'alloc_physical_order', 'map_device_mmio', 'read_bar', 'read_capabilities', 'wait_pending'):
        assert token not in irq_body
    assert 'complete_from_irq(&token, __status)' in source
    assert 'send_eoi' not in irq_body
    assert 'driver::irqchip::i8259' not in irq_body
    assert 'bigos::irq::allocate_lapic_vector(__handler, &vector)' in msix
    lapic_branch = interrupt[
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))') :
        interrupt.index('if (__detail::is_apic_spurious_vector(__frame->vector))')
    ]
    assert 'driver::irqchip::lapic::send_eoi();' in lapic_branch
    assert 'driver::irqchip::i8259::send_eoi' not in lapic_branch


def test_device_role_and_smoke_are_internal_and_default_off() -> None:
    device_h = read_source('include/bigos/device.h')
    device = read_source('kernel/core/device.cc')
    kernel = read_source('kernel/core/kernel.cc')
    xmake = read_source('xmake.lua')
    syscall_h = read_source('include/bigos/syscall.h')
    boot_debug = read_source('tools/boot_debug.py')

    assert 'VirtioBlkValidationBlock' in device_h
    assert 'DriverId::VirtioBlk' in device
    assert 'virtio_blk_probe' in device
    assert 'DeviceRole::BootBlock || __role == DeviceRole::PersistentWritableBlock' in device
    assert 'DeviceRole::VirtioBlkValidationBlock' in device
    assert 'ProbeContext::KernelInit' in device
    assert 'continue;' in device[device.index('Status probe_all') :]
    assert 'option("virtio_blk_smoke")' in xmake
    option_index = xmake.index('option("virtio_blk_smoke")')
    assert 'set_default(false)' in xmake[option_index : option_index + 220]
    assert 'add_defines("BIGOS_VIRTIO_BLK_SMOKE")' in xmake
    assert 'create_kernel_thread(&virtio_blk_smoke_entry' in kernel
    assert 'BIGOS_VIRTIO_BLK_PASSED' in read_source('kernel/drivers/block/virtio_blk.cc')
    assert 'VirtioBlkValidationBlock' not in syscall_h
    assert "'virtio_blk_smoke'" in boot_debug
    assert "case_id='modern-virtio-blk'" in boot_debug
    assert 'virtio-blk-pci,drive=virtioblk,disable-modern=off' in boot_debug
    assert 'does not replace the default ATA boot device' in boot_debug
