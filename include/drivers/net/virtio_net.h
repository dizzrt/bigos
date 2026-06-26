#ifndef _DRIVERS_NET_VIRTIO_NET_H
#define _DRIVERS_NET_VIRTIO_NET_H

#include <bigos/device.h>
#include <bigos/memory.h>
#include <drivers/pci/msix.h>
#include <drivers/virtio/pci.h>

NAMESPACE_DRIVER_BEG
namespace net {
    constexpr uint16_t VIRTIO_NET_RX_QUEUE = 0;
    constexpr uint16_t VIRTIO_NET_TX_QUEUE = 1;
    constexpr uint16_t VIRTIO_NET_QUEUE_SIZE = 16;
    constexpr uint16_t VIRTIO_NET_RX_SLOTS = 8;
    constexpr uint16_t VIRTIO_NET_TX_SLOTS = 8;
    constexpr uint32_t VIRTIO_NET_BUFFER_SIZE = 2048;
    constexpr uint16_t VIRTIO_NET_DEFAULT_MTU = 1500;
    constexpr uint32_t VIRTIO_NET_MAX_FRAME_SIZE = 1518;

    enum class VirtioNetStatus : uint8_t {
        Ok,
        InvalidArgument,
        UnsupportedContext,
        NoDevice,
        Unsupported,
        PciError,
        MappingFailed,
        AllocationFailed,
        FeatureNegotiationFailed,
        QueueConfigFailed,
        MsixFailed,
        DeviceError,
    };

    enum class VirtioNetSlotState : uint8_t {
        Free,
        Posted,
        Pending,
        Completed,
        OwnedByConsumer,
        TimedOut,
        DeviceError,
    };

    struct VirtqAvail {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
        uint16_t used_event;
    } __attribute__((packed));

    struct VirtqUsed {
        uint16_t flags;
        uint16_t idx;
        driver::virtio::VirtqUsedElem ring[VIRTIO_NET_QUEUE_SIZE];
        uint16_t avail_event;
    } __attribute__((packed));

    struct VirtioNetHeader {
        uint8_t flags;
        uint8_t gso_type;
        uint16_t header_len;
        uint16_t gso_size;
        uint16_t csum_start;
        uint16_t csum_offset;
    } __attribute__((packed));

    struct VirtioNetQueue {
        driver::virtio::QueueRef ref;
        uint64_t queue_phys;
        uint8_t *queue_mem;
        driver::virtio::VirtqDesc *desc;
        VirtqAvail *avail;
        VirtqUsed *used;
        uint16_t avail_idx;
        uint16_t last_used_idx;
    };

    struct VirtioNetRxSlot {
        uint64_t buffer_phys;
        uint8_t *buffer;
        uint32_t frame_len;
        uint32_t generation;
        VirtioNetSlotState state;
    };

    struct VirtioNetTxSlot {
        uint64_t buffer_phys;
        uint8_t *buffer;
        uint32_t frame_len;
        uint32_t generation;
        VirtioNetSlotState state;
        bigos::device::NetworkTxStatus terminal_status;
    };

    struct VirtioNetDevice {
        bigos::device::NetworkDevice net;
        driver::virtio::Transport transport;
        volatile uint32_t lock;
        bool initialized;
        bool msix_enabled;
        bool link_up;
        uint8_t mac[6];
        uint16_t mtu;
        VirtioNetQueue rx_queue;
        VirtioNetQueue tx_queue;
        VirtioNetRxSlot rx_slots[VIRTIO_NET_RX_SLOTS];
        VirtioNetTxSlot tx_slots[VIRTIO_NET_TX_SLOTS];
        driver::pci::msix::CapabilityInfo msix_info;
        driver::pci::msix::Mapping msix_mapping;
        driver::pci::msix::ProgrammedVector rx_vector;
        driver::pci::msix::ProgrammedVector tx_vector;
        bigos::device::NetworkDiagnostics diagnostics;
    };

    VirtioNetStatus virtio_net_init(VirtioNetDevice *__device) noexcept;
    void virtio_net_rx_irq() noexcept;
    void virtio_net_tx_irq() noexcept;
    const char *virtio_net_status_name(VirtioNetStatus __status) noexcept;

#ifdef BIGOS_VIRTIO_NET_SMOKE
    bool virtio_net_smoke() noexcept;
#endif
}   // namespace net
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_NET_VIRTIO_NET_H
