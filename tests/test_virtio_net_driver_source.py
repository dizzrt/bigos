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


def test_network_device_interface_is_kernel_internal_and_bounded() -> None:
    header = read_source('include/bigos/device.h')
    device = read_source('kernel/core/device.cc')
    syscall_h = read_source('include/bigos/syscall.h')

    assert 'DeviceClass::Network' in device
    assert 'VirtioNetValidation' in header
    assert 'struct NetworkDevice' in header
    assert 'NetworkTxStatus (*transmit)' in header
    assert 'NetworkRxStatus (*poll_rx)' in header
    assert 'NetworkRxStatus (*return_rx)' in header
    assert 'NetworkDiagnostics' in header
    assert 'virtio_net_probe' in device
    assert 'DeviceRole::VirtioNetValidation' in device
    assert 'NetworkDevice *network(DeviceRole __role)' in device
    assert 'VirtioNetValidation' not in syscall_h


def test_virtio_common_helper_separates_transport_from_device_semantics() -> None:
    helper_h = read_source('include/drivers/virtio/pci.h')
    helper = read_source('kernel/drivers/virtio/pci.cc')
    blk = read_source('kernel/drivers/block/virtio_blk.cc')
    net = read_source('kernel/drivers/net/virtio_net.cc')

    assert 'struct Transport' in helper_h
    assert 'parse_modern_pci_caps' in helper_h
    assert 'configure_split_queue' in helper_h
    assert 'notify_queue' in helper_h
    assert 'driver::pci::read_capabilities' in helper
    assert 'map_device_mmio' in helper
    assert 'driver::virtio::parse_modern_pci_caps' in blk
    assert 'driver::virtio::parse_modern_pci_caps' in net
    assert 'virtio_cache_round_trip' in blk
    assert 'net_transmit' in net
    assert 'net_poll_rx' in net


def test_virtio_net_probe_queue_and_completion_boundaries() -> None:
    header = read_source('include/drivers/net/virtio_net.h')
    source = read_source('kernel/drivers/net/virtio_net.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert 'PCI_MODERN_NET_DEVICE_ID' in source
    assert 'PCI_TRANSITIONAL_NET_DEVICE_ID' in source
    assert 'VIRTIO_NET_RX_QUEUE = 0' in header
    assert 'VIRTIO_NET_TX_QUEUE = 1' in header
    assert 'VIRTIO_NET_RX_SLOTS = 8' in header
    assert 'VIRTIO_NET_TX_SLOTS = 8' in header
    assert 'generation' in header
    assert 'post_rx_slot' in source
    assert 'Malformed' in source
    assert 'StaleGeneration' in source
    assert 'driver::pci::msix::allocate_and_program_entry' in source
    assert 'driver::virtio::set_queue_msix_vector' in source

    rx_irq = source[source.index('void virtio_net_rx_irq') : source.index('void virtio_net_tx_irq')]
    tx_irq = source[source.index('void virtio_net_tx_irq') : source.index('const char *virtio_net_status_name')]
    for irq_body in (rx_irq, tx_irq):
        for token in (
            'kmalloc',
            'alloc_kernel_pages',
            'alloc_physical_order',
            'map_device_mmio',
            'read_capabilities',
            'wait_pending',
            'bigos::vfs',
            'syscall',
        ):
            assert token not in irq_body
        assert 'send_eoi' not in irq_body
        assert 'driver::irqchip::i8259' not in irq_body

    lapic_branch = interrupt[
        interrupt.index('if (__detail::is_lapic_external_irq(__frame->vector))') : interrupt.index(
            'if (__detail::is_apic_spurious_vector(__frame->vector))'
        )
    ]
    assert 'driver::irqchip::lapic::send_eoi();' in lapic_branch
    assert 'driver::irqchip::i8259::send_eoi' not in lapic_branch


def test_virtio_net_smoke_and_tap_helper_are_default_off() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')
    tools = read_source('tools/bigosdev/core.py')
    tap = read_source('tools/virtio_net_tap.py')

    assert 'option("virtio_net_smoke")' in xmake
    option_index = xmake.index('option("virtio_net_smoke")')
    assert 'set_default(false)' in xmake[option_index : option_index + 240]
    assert 'add_defines("BIGOS_VIRTIO_NET_SMOKE")' in xmake
    assert 'create_kernel_thread(&virtio_net_smoke_entry' in kernel
    assert "case_id='modern-virtio-net'" in tools
    assert 'virtio-net-pci,netdev=bigosnet,disable-modern=off' in tools
    assert 'modern_network_validation_case' in tools
    assert 'TAP-backed virtio-net smoke requires Linux host TAP support' in tools
    assert 'BIGOS_VIRTIO_NET_TAP_READY' in tap
    assert 'BIGOS_VIRTIO_NET_TAP_CLEANED' in tap
