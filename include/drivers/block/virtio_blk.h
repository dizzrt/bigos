#ifndef _DRIVERS_BLOCK_VIRTIO_BLK_H
#define _DRIVERS_BLOCK_VIRTIO_BLK_H

#include <bigos/block_io.h>
#include <bigos/memory.h>
#include <drivers/block/block_device.h>
#include <drivers/pci/config.h>
#include <drivers/pci/msix.h>

NAMESPACE_DRIVER_BEG
namespace block {
    constexpr uint32_t VIRTIO_BLK_SECTOR_SIZE = DEFAULT_SECTOR_SIZE;
    constexpr uint16_t VIRTIO_BLK_QUEUE_SIZE = 32;
    constexpr uint16_t VIRTIO_BLK_REQUEST_SLOTS = bigos::block_io::QUEUE_CAPACITY_PER_DEVICE;

    enum class VirtioBlkStatus : uint8_t {
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

    struct VirtqDesc {
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        uint16_t next;
    } __attribute__((packed));

    struct VirtqAvail {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[VIRTIO_BLK_QUEUE_SIZE];
        uint16_t used_event;
    } __attribute__((packed));

    struct VirtqUsedElem {
        uint32_t id;
        uint32_t len;
    } __attribute__((packed));

    struct VirtqUsed {
        uint16_t flags;
        uint16_t idx;
        VirtqUsedElem ring[VIRTIO_BLK_QUEUE_SIZE];
        uint16_t avail_event;
    } __attribute__((packed));

    struct VirtioBlkRequestHeader {
        uint32_t type;
        uint32_t reserved;
        uint64_t sector;
    } __attribute__((packed));

    struct VirtioBlkSlot {
        VirtioBlkRequestHeader header;
        uint8_t status;
        uint64_t header_phys;
        uint64_t data_phys;
        uint64_t status_phys;
        VirtioBlkRequestHeader *dma_header;
        uint8_t *data;
        uint8_t *dma_status;
        bigos::block_io::CompletionToken token;
        bigos::block_io::Request *request;
        bool active;
        bool reading;
    };

    struct VirtioBlkDevice {
        BlockDevice block;
        driver::pci::FunctionAddress pci_address;
        volatile uint32_t lock;
        bool initialized;
        bool msix_enabled;
        uint16_t queue_size;
        uint16_t avail_idx;
        uint16_t last_used_idx;
        uint16_t notify_offset;
        uint32_t notify_multiplier;
        uint64_t capacity_sectors;
        bigos::mm::DeviceMmioMapping common_mapping;
        bigos::mm::DeviceMmioMapping notify_mapping;
        bigos::mm::DeviceMmioMapping isr_mapping;
        bigos::mm::DeviceMmioMapping device_mapping;
        volatile void *common_cfg;
        volatile uint16_t *notify;
        volatile uint8_t *isr_cfg;
        volatile void *device_cfg;
        uint64_t queue_phys;
        uint8_t *queue_mem;
        VirtqDesc *desc;
        VirtqAvail *avail;
        VirtqUsed *used;
        VirtioBlkSlot slots[VIRTIO_BLK_REQUEST_SLOTS];
        driver::pci::msix::CapabilityInfo msix_info;
        driver::pci::msix::Mapping msix_mapping;
        driver::pci::msix::ProgrammedVector completion_vector;
    };

    VirtioBlkStatus virtio_blk_init(VirtioBlkDevice *__device) noexcept;
    void virtio_blk_irq() noexcept;
    const char *virtio_blk_status_name(VirtioBlkStatus __status) noexcept;

#ifdef BIGOS_VIRTIO_BLK_SMOKE
    bool virtio_blk_smoke() noexcept;
#endif
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_BLOCK_VIRTIO_BLK_H
