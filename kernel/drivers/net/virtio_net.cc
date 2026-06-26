#include <drivers/net/virtio_net.h>

#include "../../mm/buddy.h"

#include <bigos/io.h>
#include <bigos/sched.h>
#include <bigos/timer.h>
#include <drivers/irqchip/lapic.h>
#include <string.h>

namespace {
    constexpr uint64_t VIRTIO_NET_F_CSUM = 0;
    constexpr uint64_t VIRTIO_NET_F_MTU = 3;
    constexpr uint64_t VIRTIO_NET_F_MAC = 5;
    constexpr uint64_t VIRTIO_NET_F_STATUS = 16;
    constexpr uint16_t VIRTIO_NET_S_LINK_UP = 1u;
    constexpr uint32_t VIRTIO_QUEUE_PAGE_ORDER = 0;

    struct VirtioNetConfig {
        volatile uint8_t mac[6];
        volatile uint16_t status;
        volatile uint16_t max_virtqueue_pairs;
        volatile uint16_t mtu;
    } __attribute__((packed));

    driver::net::VirtioNetDevice *g_irq_device = nullptr;

    void spin_lock(volatile uint32_t *__lock) noexcept {
        while (__atomic_exchange_n(__lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
            while (__atomic_load_n(__lock, __ATOMIC_RELAXED) != 0u)
                asm volatile("pause" ::: "memory");
        }
    }

    void spin_unlock(volatile uint32_t *__lock) noexcept {
        __atomic_store_n(__lock, 0u, __ATOMIC_RELEASE);
    }

    driver::net::VirtioNetStatus from_virtio_status(driver::virtio::Status __status) noexcept {
        switch (__status) {
            case driver::virtio::Status::Ok:
                return driver::net::VirtioNetStatus::Ok;
            case driver::virtio::Status::InvalidArgument:
                return driver::net::VirtioNetStatus::InvalidArgument;
            case driver::virtio::Status::UnsupportedContext:
                return driver::net::VirtioNetStatus::UnsupportedContext;
            case driver::virtio::Status::NoDevice:
                return driver::net::VirtioNetStatus::NoDevice;
            case driver::virtio::Status::Unsupported:
                return driver::net::VirtioNetStatus::Unsupported;
            case driver::virtio::Status::MappingFailed:
                return driver::net::VirtioNetStatus::MappingFailed;
            case driver::virtio::Status::FeatureNegotiationFailed:
                return driver::net::VirtioNetStatus::FeatureNegotiationFailed;
            case driver::virtio::Status::QueueConfigFailed:
                return driver::net::VirtioNetStatus::QueueConfigFailed;
            case driver::virtio::Status::DeviceError:
                return driver::net::VirtioNetStatus::DeviceError;
            default:
                return driver::net::VirtioNetStatus::PciError;
        }
    }

    driver::net::VirtioNetStatus from_msix_status(driver::pci::msix::Status __status) noexcept {
        switch (__status) {
            case driver::pci::msix::Status::Ok:
                return driver::net::VirtioNetStatus::Ok;
            case driver::pci::msix::Status::UnsupportedContext:
                return driver::net::VirtioNetStatus::UnsupportedContext;
            case driver::pci::msix::Status::NoDevice:
                return driver::net::VirtioNetStatus::NoDevice;
            case driver::pci::msix::Status::Unsupported:
                return driver::net::VirtioNetStatus::Unsupported;
            case driver::pci::msix::Status::MappingFailed:
                return driver::net::VirtioNetStatus::MappingFailed;
            default:
                return driver::net::VirtioNetStatus::MsixFailed;
        }
    }

    bool find_device(driver::pci::FunctionAddress *__out) noexcept {
        if (__out == nullptr)
            return false;
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                const driver::pci::FunctionAddress address = {0, dev, func};
                driver::pci::DeviceId id = {};
                if (driver::pci::probe_device(address, &id) != driver::pci::Status::Ok)
                    continue;
                if (id.vendor_id == driver::virtio::PCI_VENDOR_ID &&
                    id.device_id == driver::virtio::PCI_MODERN_NET_DEVICE_ID) {
                    *__out = address;
                    return true;
                }
                if (id.vendor_id == driver::virtio::PCI_VENDOR_ID &&
                    id.device_id == driver::virtio::PCI_TRANSITIONAL_NET_DEVICE_ID)
                    return false;
            }
        }
        return false;
    }

    bool alloc_queue(driver::net::VirtioNetQueue *__queue) noexcept {
        void *queue_phys = bigos::mm::__detail::alloc_physical_order(VIRTIO_QUEUE_PAGE_ORDER, 0);
        if (queue_phys == nullptr)
            return false;
        __queue->queue_phys = (uint64_t)queue_phys;
        __queue->queue_mem = (uint8_t *)bigos::mm::phys_to_direct(__queue->queue_phys);
        if (__queue->queue_mem == nullptr)
            return false;
        memset(__queue->queue_mem, 0, 4096);

        uint8_t *cursor = __queue->queue_mem;
        __queue->desc = (driver::virtio::VirtqDesc *)cursor;
        cursor += sizeof(driver::virtio::VirtqDesc) * driver::net::VIRTIO_NET_QUEUE_SIZE;
        __queue->avail = (driver::net::VirtqAvail *)cursor;
        cursor += sizeof(driver::net::VirtqAvail);
        cursor = (uint8_t *)(((uint64_t)cursor + 3ull) & ~3ull);
        __queue->used = (driver::net::VirtqUsed *)cursor;
        __queue->avail_idx = 0;
        __queue->last_used_idx = 0;
        return true;
    }

    bool alloc_packet_buffers(driver::net::VirtioNetDevice *__device) noexcept {
        for (uint16_t i = 0; i < driver::net::VIRTIO_NET_RX_SLOTS; i++) {
            void *phys = bigos::mm::__detail::alloc_physical_order(0, 0);
            if (phys == nullptr)
                return false;
            __device->rx_slots[i].buffer_phys = (uint64_t)phys;
            __device->rx_slots[i].buffer = (uint8_t *)bigos::mm::phys_to_direct((uint64_t)phys);
            if (__device->rx_slots[i].buffer == nullptr)
                return false;
            memset(__device->rx_slots[i].buffer, 0, 4096);
            __device->rx_slots[i].state = driver::net::VirtioNetSlotState::Free;
            __device->rx_slots[i].generation = 1;
        }
        for (uint16_t i = 0; i < driver::net::VIRTIO_NET_TX_SLOTS; i++) {
            void *phys = bigos::mm::__detail::alloc_physical_order(0, 0);
            if (phys == nullptr)
                return false;
            __device->tx_slots[i].buffer_phys = (uint64_t)phys;
            __device->tx_slots[i].buffer = (uint8_t *)bigos::mm::phys_to_direct((uint64_t)phys);
            if (__device->tx_slots[i].buffer == nullptr)
                return false;
            memset(__device->tx_slots[i].buffer, 0, 4096);
            __device->tx_slots[i].state = driver::net::VirtioNetSlotState::Free;
            __device->tx_slots[i].generation = 1;
        }
        return true;
    }

    void post_rx_slot(driver::net::VirtioNetDevice *__device, uint16_t __slot) noexcept {
        driver::net::VirtioNetRxSlot &slot = __device->rx_slots[__slot];
        slot.state = driver::net::VirtioNetSlotState::Posted;
        slot.frame_len = 0;
        __device->rx_queue.desc[__slot] = {
            slot.buffer_phys,
            driver::net::VIRTIO_NET_BUFFER_SIZE,
            driver::virtio::VIRTQ_DESC_F_WRITE,
            0,
        };
        driver::virtio::compiler_barrier();
        __device->rx_queue.avail->ring[__device->rx_queue.avail_idx % __device->rx_queue.ref.size] = __slot;
        driver::virtio::compiler_barrier();
        __device->rx_queue.avail_idx++;
        __device->rx_queue.avail->idx = __device->rx_queue.avail_idx;
        driver::virtio::notify_queue(__device->rx_queue.ref);
    }

    driver::net::VirtioNetStatus configure_queues(driver::net::VirtioNetDevice *__device) noexcept {
        if (!alloc_queue(&__device->rx_queue) || !alloc_queue(&__device->tx_queue) || !alloc_packet_buffers(__device))
            return driver::net::VirtioNetStatus::AllocationFailed;

        driver::virtio::Status status = driver::virtio::configure_split_queue(&__device->transport,
            driver::net::VIRTIO_NET_RX_QUEUE, driver::net::VIRTIO_NET_QUEUE_SIZE, __device->rx_queue.queue_phys,
            __device->rx_queue.queue_phys + ((uint8_t *)__device->rx_queue.avail - __device->rx_queue.queue_mem),
            __device->rx_queue.queue_phys + ((uint8_t *)__device->rx_queue.used - __device->rx_queue.queue_mem),
            &__device->rx_queue.ref);
        if (status == driver::virtio::Status::Ok)
            status = driver::virtio::configure_split_queue(&__device->transport, driver::net::VIRTIO_NET_TX_QUEUE,
                driver::net::VIRTIO_NET_QUEUE_SIZE, __device->tx_queue.queue_phys,
                __device->tx_queue.queue_phys + ((uint8_t *)__device->tx_queue.avail - __device->tx_queue.queue_mem),
                __device->tx_queue.queue_phys + ((uint8_t *)__device->tx_queue.used - __device->tx_queue.queue_mem),
                &__device->tx_queue.ref);
        if (status != driver::virtio::Status::Ok)
            return from_virtio_status(status);

        for (uint16_t i = 0; i < driver::net::VIRTIO_NET_RX_SLOTS; i++)
            post_rx_slot(__device, i);
        return driver::net::VirtioNetStatus::Ok;
    }

    driver::net::VirtioNetStatus configure_msix(driver::net::VirtioNetDevice *__device) noexcept {
        driver::pci::msix::Status status =
            driver::pci::msix::find_capability(__device->transport.pci_address, &__device->msix_info);
        if (status != driver::pci::msix::Status::Ok)
            return from_msix_status(status);
        status = driver::pci::msix::set_function_mask(__device->msix_info, true);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::map_table_and_pba(__device->msix_info, &__device->msix_mapping);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::allocate_and_program_entry(__device->msix_mapping, 0,
                [](bigos::irq::InterruptFrame *) noexcept { driver::net::virtio_net_rx_irq(); },
                driver::irqchip::lapic::id(), &__device->rx_vector);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::allocate_and_program_entry(__device->msix_mapping, 1,
                [](bigos::irq::InterruptFrame *) noexcept { driver::net::virtio_net_tx_irq(); },
                driver::irqchip::lapic::id(), &__device->tx_vector);
        if (status != driver::pci::msix::Status::Ok)
            return from_msix_status(status);

        driver::virtio::Status virtio_status = driver::virtio::set_queue_msix_vector(
            &__device->transport, driver::net::VIRTIO_NET_RX_QUEUE, __device->rx_vector.entry_index);
        if (virtio_status == driver::virtio::Status::Ok)
            virtio_status = driver::virtio::set_queue_msix_vector(
                &__device->transport, driver::net::VIRTIO_NET_TX_QUEUE, __device->tx_vector.entry_index);
        if (virtio_status != driver::virtio::Status::Ok)
            return driver::net::VirtioNetStatus::MsixFailed;

        status = driver::pci::msix::set_enable(__device->msix_info, true);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::set_entry_mask(__device->msix_mapping, __device->rx_vector.entry_index, false);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::set_entry_mask(__device->msix_mapping, __device->tx_vector.entry_index, false);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::set_function_mask(__device->msix_info, false);
        if (status != driver::pci::msix::Status::Ok)
            return from_msix_status(status);
        __device->msix_enabled = true;
        return driver::net::VirtioNetStatus::Ok;
    }

    driver::net::VirtioNetStatus negotiate_features(driver::net::VirtioNetDevice *__device) noexcept {
        driver::virtio::Status status = driver::virtio::reset_and_start_driver(&__device->transport);
        if (status != driver::virtio::Status::Ok)
            return from_virtio_status(status);
        if (!driver::virtio::read_device_feature_bit(&__device->transport, driver::virtio::F_VERSION_1_BIT))
            return driver::net::VirtioNetStatus::FeatureNegotiationFailed;

        uint32_t features0 = 0;
        if (driver::virtio::read_device_feature_bit(&__device->transport, VIRTIO_NET_F_MAC))
            features0 |= 1u << VIRTIO_NET_F_MAC;
        if (driver::virtio::read_device_feature_bit(&__device->transport, VIRTIO_NET_F_STATUS))
            features0 |= 1u << VIRTIO_NET_F_STATUS;
        if (driver::virtio::read_device_feature_bit(&__device->transport, VIRTIO_NET_F_MTU))
            features0 |= 1u << VIRTIO_NET_F_MTU;
        if (driver::virtio::read_device_feature_bit(&__device->transport, VIRTIO_NET_F_CSUM))
            features0 |= 1u << VIRTIO_NET_F_CSUM;
        driver::virtio::write_driver_features(&__device->transport, 0, features0);
        driver::virtio::write_driver_features(
            &__device->transport, 1, 1u << (driver::virtio::F_VERSION_1_BIT % 32));
        status = driver::virtio::accept_features(&__device->transport);
        return from_virtio_status(status);
    }

    bool net_ready(bigos::device::NetworkDevice *__net) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        return dev != nullptr && dev->initialized;
    }

    bool net_link_up(bigos::device::NetworkDevice *__net) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        return dev != nullptr && dev->initialized && dev->link_up;
    }

    const uint8_t *net_mac(bigos::device::NetworkDevice *__net) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        return dev == nullptr ? nullptr : dev->mac;
    }

    uint16_t net_mtu(bigos::device::NetworkDevice *__net) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        return dev == nullptr ? 0 : dev->mtu;
    }

    bigos::device::NetworkTxStatus net_transmit(
        bigos::device::NetworkDevice *__net, const void *__frame, uint32_t __len, uint64_t __timeout_ticks) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        if (dev == nullptr || !dev->initialized) {
            return bigos::device::NetworkTxStatus::NotReady;
        }
        if (__frame == nullptr || __len == 0 || __len > driver::net::VIRTIO_NET_MAX_FRAME_SIZE) {
            dev->diagnostics.tx_rejected++;
            dev->diagnostics.last_tx_status = bigos::device::NetworkTxStatus::InvalidFrame;
            return bigos::device::NetworkTxStatus::InvalidFrame;
        }

        uint16_t slot_index = UINT16_MAX;
        {
            bigos::irq::InterruptGuard guard;
            spin_lock(&dev->lock);
            for (uint16_t i = 0; i < driver::net::VIRTIO_NET_TX_SLOTS; i++) {
                if (dev->tx_slots[i].state == driver::net::VirtioNetSlotState::Free ||
                    dev->tx_slots[i].state == driver::net::VirtioNetSlotState::Completed ||
                    dev->tx_slots[i].state == driver::net::VirtioNetSlotState::TimedOut ||
                    dev->tx_slots[i].state == driver::net::VirtioNetSlotState::DeviceError) {
                    slot_index = i;
                    break;
                }
            }
            if (slot_index == UINT16_MAX) {
                dev->diagnostics.tx_rejected++;
                dev->diagnostics.last_tx_status = bigos::device::NetworkTxStatus::NoSlot;
                spin_unlock(&dev->lock);
                return bigos::device::NetworkTxStatus::NoSlot;
            }

            driver::net::VirtioNetTxSlot &slot = dev->tx_slots[slot_index];
            slot.generation++;
            slot.frame_len = __len;
            slot.state = driver::net::VirtioNetSlotState::Pending;
            slot.terminal_status = bigos::device::NetworkTxStatus::Timeout;
            memset(slot.buffer, 0, sizeof(driver::net::VirtioNetHeader));
            memcpy(slot.buffer + sizeof(driver::net::VirtioNetHeader), __frame, __len);

            const uint16_t head = (uint16_t)(slot_index * 2);
            dev->tx_queue.desc[head] = {
                slot.buffer_phys,
                sizeof(driver::net::VirtioNetHeader),
                driver::virtio::VIRTQ_DESC_F_NEXT,
                (uint16_t)(head + 1),
            };
            dev->tx_queue.desc[head + 1] = {
                slot.buffer_phys + sizeof(driver::net::VirtioNetHeader),
                __len,
                0,
                0,
            };
            driver::virtio::compiler_barrier();
            dev->tx_queue.avail->ring[dev->tx_queue.avail_idx % dev->tx_queue.ref.size] = head;
            driver::virtio::compiler_barrier();
            dev->tx_queue.avail_idx++;
            dev->tx_queue.avail->idx = dev->tx_queue.avail_idx;
            dev->diagnostics.tx_submitted++;
            driver::virtio::notify_queue(dev->tx_queue.ref);
            spin_unlock(&dev->lock);
        }

        const uint64_t start = bigos::timer::ticks();
        const uint64_t timeout = __timeout_ticks == 0 ? 20 : __timeout_ticks;
        for (;;) {
            bigos::irq::InterruptGuard guard;
            spin_lock(&dev->lock);
            driver::net::VirtioNetTxSlot &slot = dev->tx_slots[slot_index];
            const driver::net::VirtioNetSlotState state = slot.state;
            const bigos::device::NetworkTxStatus terminal = slot.terminal_status;
            if (state == driver::net::VirtioNetSlotState::Completed || state == driver::net::VirtioNetSlotState::DeviceError) {
                slot.state = driver::net::VirtioNetSlotState::Free;
                dev->diagnostics.last_tx_status = terminal;
                spin_unlock(&dev->lock);
                return terminal;
            }
            if (bigos::timer::ticks() - start >= timeout) {
                slot.state = driver::net::VirtioNetSlotState::TimedOut;
                slot.terminal_status = bigos::device::NetworkTxStatus::Timeout;
                dev->diagnostics.tx_timeout++;
                dev->diagnostics.last_tx_status = bigos::device::NetworkTxStatus::Timeout;
                spin_unlock(&dev->lock);
                return bigos::device::NetworkTxStatus::Timeout;
            }
            spin_unlock(&dev->lock);
            bigos::sched::yield();
        }
    }

    bigos::device::NetworkRxStatus net_poll_rx(
        bigos::device::NetworkDevice *__net, bigos::device::NetworkRxFrame *__out) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        if (dev == nullptr || !dev->initialized)
            return bigos::device::NetworkRxStatus::NotReady;
        if (__out == nullptr)
            return bigos::device::NetworkRxStatus::InvalidArgument;

        bigos::irq::InterruptGuard guard;
        spin_lock(&dev->lock);
        for (uint16_t i = 0; i < driver::net::VIRTIO_NET_RX_SLOTS; i++) {
            driver::net::VirtioNetRxSlot &slot = dev->rx_slots[i];
            if (slot.state != driver::net::VirtioNetSlotState::Completed)
                continue;
            slot.state = driver::net::VirtioNetSlotState::OwnedByConsumer;
            __out->data = slot.buffer + sizeof(driver::net::VirtioNetHeader);
            __out->length = slot.frame_len;
            __out->slot = i;
            __out->generation = slot.generation;
            dev->diagnostics.last_rx_status = bigos::device::NetworkRxStatus::Success;
            spin_unlock(&dev->lock);
            return bigos::device::NetworkRxStatus::Success;
        }
        dev->diagnostics.last_rx_status = bigos::device::NetworkRxStatus::NoFrame;
        spin_unlock(&dev->lock);
        return bigos::device::NetworkRxStatus::NoFrame;
    }

    bigos::device::NetworkRxStatus net_return_rx(
        bigos::device::NetworkDevice *__net, const bigos::device::NetworkRxFrame *__frame) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        if (dev == nullptr || !dev->initialized)
            return bigos::device::NetworkRxStatus::NotReady;
        if (__frame == nullptr || __frame->slot >= driver::net::VIRTIO_NET_RX_SLOTS)
            return bigos::device::NetworkRxStatus::InvalidArgument;

        bigos::irq::InterruptGuard guard;
        spin_lock(&dev->lock);
        driver::net::VirtioNetRxSlot &slot = dev->rx_slots[__frame->slot];
        if (slot.generation != __frame->generation) {
            dev->diagnostics.completion_rejected++;
            dev->diagnostics.last_rx_status = bigos::device::NetworkRxStatus::StaleGeneration;
            spin_unlock(&dev->lock);
            return bigos::device::NetworkRxStatus::StaleGeneration;
        }
        if (slot.state != driver::net::VirtioNetSlotState::OwnedByConsumer) {
            dev->diagnostics.last_rx_status = bigos::device::NetworkRxStatus::OwnershipError;
            spin_unlock(&dev->lock);
            return bigos::device::NetworkRxStatus::OwnershipError;
        }
        slot.generation++;
        post_rx_slot(dev, __frame->slot);
        dev->diagnostics.last_rx_status = bigos::device::NetworkRxStatus::Success;
        spin_unlock(&dev->lock);
        return bigos::device::NetworkRxStatus::Success;
    }

    void net_diagnostics(
        bigos::device::NetworkDevice *__net, bigos::device::NetworkDiagnostics *__out) noexcept {
        driver::net::VirtioNetDevice *dev = __net == nullptr ? nullptr : (driver::net::VirtioNetDevice *)__net->context;
        if (dev == nullptr || __out == nullptr)
            return;
        *__out = dev->diagnostics;
    }
}   // namespace

NAMESPACE_DRIVER_BEG
namespace net {
    VirtioNetStatus virtio_net_init(VirtioNetDevice *__device) noexcept {
        if (__device == nullptr)
            return VirtioNetStatus::InvalidArgument;
        if (!driver::pci::context_allows_config_access() || !driver::pci::msix::context_allows_msix_config())
            return VirtioNetStatus::UnsupportedContext;

        *__device = {};
        if (!find_device(&__device->transport.pci_address))
            return VirtioNetStatus::NoDevice;

        driver::virtio::Status virtio_status =
            driver::virtio::enable_modern_pci_command(__device->transport.pci_address);
        if (virtio_status != driver::virtio::Status::Ok)
            return from_virtio_status(virtio_status);

        driver::virtio::PciCaps caps = {};
        virtio_status = driver::virtio::parse_modern_pci_caps(__device->transport.pci_address, &caps);
        if (virtio_status == driver::virtio::Status::Ok)
            virtio_status = driver::virtio::map_caps(&__device->transport, caps);
        if (virtio_status != driver::virtio::Status::Ok)
            return from_virtio_status(virtio_status);

        VirtioNetStatus status = negotiate_features(__device);
        if (status == VirtioNetStatus::Ok)
            status = configure_queues(__device);
        if (status == VirtioNetStatus::Ok)
            status = configure_msix(__device);
        if (status != VirtioNetStatus::Ok) {
            driver::virtio::set_failed(&__device->transport);
            return status;
        }

        VirtioNetConfig *config = (VirtioNetConfig *)__device->transport.device_cfg;
        for (uint32_t i = 0; i < sizeof(__device->mac); i++)
            __device->mac[i] = config->mac[i];
        __device->mtu = config->mtu == 0 ? VIRTIO_NET_DEFAULT_MTU : config->mtu;
        __device->link_up = (config->status & VIRTIO_NET_S_LINK_UP) != 0;

        __device->net.context = __device;
        __device->net.ready = &net_ready;
        __device->net.link_up = &net_link_up;
        __device->net.mac = &net_mac;
        __device->net.mtu = &net_mtu;
        __device->net.transmit = &net_transmit;
        __device->net.poll_rx = &net_poll_rx;
        __device->net.return_rx = &net_return_rx;
        __device->net.diagnostics = &net_diagnostics;
        __device->initialized = true;
        g_irq_device = __device;
        driver::virtio::set_driver_ok(&__device->transport);
        bigos::serial_puts("BIGOS_VIRTIO_NET_PUBLISHED\n");
        return VirtioNetStatus::Ok;
    }

    void virtio_net_rx_irq() noexcept {
        VirtioNetDevice *dev = g_irq_device;
        if (dev == nullptr || !dev->initialized)
            return;
        spin_lock(&dev->lock);
        const uint16_t used_idx = dev->rx_queue.used->idx;
        while (dev->rx_queue.last_used_idx != used_idx) {
            const driver::virtio::VirtqUsedElem elem =
                dev->rx_queue.used->ring[dev->rx_queue.last_used_idx % dev->rx_queue.ref.size];
            dev->rx_queue.last_used_idx++;
            if (elem.id >= VIRTIO_NET_RX_SLOTS || dev->rx_slots[elem.id].state != VirtioNetSlotState::Posted) {
                dev->diagnostics.completion_rejected++;
                bigos::serial_puts("BIGOS_VIRTIO_NET_RX_COMPLETION_REJECTED slot\n");
                continue;
            }
            VirtioNetRxSlot &slot = dev->rx_slots[elem.id];
            if (elem.len <= sizeof(VirtioNetHeader) || elem.len > VIRTIO_NET_BUFFER_SIZE) {
                dev->diagnostics.rx_malformed++;
                dev->diagnostics.last_rx_status = bigos::device::NetworkRxStatus::Malformed;
                post_rx_slot(dev, (uint16_t)elem.id);
                continue;
            }
            slot.frame_len = elem.len - sizeof(VirtioNetHeader);
            slot.state = VirtioNetSlotState::Completed;
            dev->diagnostics.rx_completed++;
        }
        spin_unlock(&dev->lock);
    }

    void virtio_net_tx_irq() noexcept {
        VirtioNetDevice *dev = g_irq_device;
        if (dev == nullptr || !dev->initialized)
            return;
        spin_lock(&dev->lock);
        const uint16_t used_idx = dev->tx_queue.used->idx;
        while (dev->tx_queue.last_used_idx != used_idx) {
            const driver::virtio::VirtqUsedElem elem =
                dev->tx_queue.used->ring[dev->tx_queue.last_used_idx % dev->tx_queue.ref.size];
            dev->tx_queue.last_used_idx++;
            if (elem.id % 2 != 0) {
                dev->diagnostics.completion_rejected++;
                bigos::serial_puts("BIGOS_VIRTIO_NET_TX_COMPLETION_REJECTED descriptor\n");
                continue;
            }
            const uint16_t slot_index = (uint16_t)(elem.id / 2);
            if (slot_index >= VIRTIO_NET_TX_SLOTS) {
                dev->diagnostics.completion_rejected++;
                bigos::serial_puts("BIGOS_VIRTIO_NET_TX_COMPLETION_REJECTED slot\n");
                continue;
            }
            VirtioNetTxSlot &slot = dev->tx_slots[slot_index];
            if (slot.state != VirtioNetSlotState::Pending) {
                dev->diagnostics.completion_rejected++;
                bigos::serial_puts("BIGOS_VIRTIO_NET_TX_COMPLETION_REJECTED state\n");
                continue;
            }
            slot.terminal_status = bigos::device::NetworkTxStatus::Success;
            slot.state = VirtioNetSlotState::Completed;
            dev->diagnostics.tx_completed++;
        }
        spin_unlock(&dev->lock);
    }

    const char *virtio_net_status_name(VirtioNetStatus __status) noexcept {
        switch (__status) {
            case VirtioNetStatus::Ok:
                return "ok";
            case VirtioNetStatus::InvalidArgument:
                return "invalid-argument";
            case VirtioNetStatus::UnsupportedContext:
                return "unsupported-context";
            case VirtioNetStatus::NoDevice:
                return "no-device";
            case VirtioNetStatus::Unsupported:
                return "unsupported";
            case VirtioNetStatus::PciError:
                return "pci-error";
            case VirtioNetStatus::MappingFailed:
                return "mapping-failed";
            case VirtioNetStatus::AllocationFailed:
                return "allocation-failed";
            case VirtioNetStatus::FeatureNegotiationFailed:
                return "feature-negotiation-failed";
            case VirtioNetStatus::QueueConfigFailed:
                return "queue-config-failed";
            case VirtioNetStatus::MsixFailed:
                return "msix-failed";
            case VirtioNetStatus::DeviceError:
                return "device-error";
            default:
                return "unknown";
        }
    }

#ifdef BIGOS_VIRTIO_NET_SMOKE
    bool virtio_net_smoke() noexcept {
        const bigos::device::Status probe_status = bigos::device::probe(bigos::device::DeviceClass::Network,
            bigos::device::DeviceRole::VirtioNetValidation, bigos::device::ProbeContext::OrdinaryBlockable);
        if (probe_status != bigos::device::Status::Success && probe_status != bigos::device::Status::ProbeFailed) {
            bigos::serial_puts("BIGOS_VIRTIO_NET_SKIPPED probe-context\n");
            return true;
        }
        bigos::device::NetworkDevice *net = bigos::device::network(bigos::device::DeviceRole::VirtioNetValidation);
        if (net == nullptr || net->ready == nullptr || !net->ready(net)) {
            bigos::serial_puts("BIGOS_VIRTIO_NET_SKIPPED unavailable\n");
            return true;
        }

        uint8_t frame[64] = {};
        for (uint32_t i = 0; i < sizeof(frame); i++)
            frame[i] = (uint8_t)(0x50u ^ i);
        bigos::device::NetworkTxStatus tx = net->transmit(net, frame, sizeof(frame), 30);
        if (tx != bigos::device::NetworkTxStatus::Success) {
            bigos::serial_puts("BIGOS_VIRTIO_NET_FAILED tx\n");
            return false;
        }
        tx = net->transmit(net, nullptr, sizeof(frame), 1);
        if (tx != bigos::device::NetworkTxStatus::InvalidFrame) {
            bigos::serial_puts("BIGOS_VIRTIO_NET_FAILED invalid-tx\n");
            return false;
        }

        bigos::device::NetworkRxFrame rx = {};
        const uint64_t start = bigos::timer::ticks();
        while (net->poll_rx(net, &rx) != bigos::device::NetworkRxStatus::Success &&
               bigos::timer::ticks() - start < 60)
            bigos::sched::yield();
        if (rx.data == nullptr || rx.length == 0) {
            bigos::serial_puts("BIGOS_VIRTIO_NET_SKIPPED rx-timeout\n");
            return true;
        }
        if (net->return_rx(net, &rx) != bigos::device::NetworkRxStatus::Success) {
            bigos::serial_puts("BIGOS_VIRTIO_NET_FAILED rx-return\n");
            return false;
        }

        bigos::serial_puts("BIGOS_VIRTIO_NET_PASSED\n");
        return true;
    }
#endif
}   // namespace net
NAMESPACE_DRIVER_END
