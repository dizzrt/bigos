#include <drivers/block/virtio_blk.h>

#include "../../mm/buddy.h"

#include <bigos/device.h>
#include <bigos/fs/bcache.h>
#include <bigos/io.h>
#include <drivers/irqchip/lapic.h>
#include <string.h>

namespace {
    constexpr uint16_t VIRTIO_PCI_VENDOR_ID = 0x1af4;
    constexpr uint16_t VIRTIO_PCI_TRANSITIONAL_BLK_DEVICE_ID = 0x1001;
    constexpr uint16_t VIRTIO_PCI_MODERN_BLK_DEVICE_ID = 0x1042;
    constexpr uint8_t VIRTIO_PCI_CAPABILITY_ID = 0x09;
    constexpr uint8_t VIRTIO_PCI_CAP_COMMON_CFG = 1;
    constexpr uint8_t VIRTIO_PCI_CAP_NOTIFY_CFG = 2;
    constexpr uint8_t VIRTIO_PCI_CAP_ISR_CFG = 3;
    constexpr uint8_t VIRTIO_PCI_CAP_DEVICE_CFG = 4;

    constexpr uint8_t PCI_REG_COMMAND = 0x04;
    constexpr uint16_t PCI_COMMAND_IO_SPACE = 1u << 0;
    constexpr uint16_t PCI_COMMAND_MEMORY_SPACE = 1u << 1;
    constexpr uint16_t PCI_COMMAND_BUS_MASTER = 1u << 2;

    constexpr uint64_t VIRTIO_F_VERSION_1_BIT = 32;
    constexpr uint32_t VIRTIO_STATUS_ACKNOWLEDGE = 1u;
    constexpr uint32_t VIRTIO_STATUS_DRIVER = 2u;
    constexpr uint32_t VIRTIO_STATUS_DRIVER_OK = 4u;
    constexpr uint32_t VIRTIO_STATUS_FEATURES_OK = 8u;
    constexpr uint32_t VIRTIO_STATUS_FAILED = 128u;

    constexpr uint32_t VIRTIO_BLK_T_IN = 0;
    constexpr uint32_t VIRTIO_BLK_T_OUT = 1;
    constexpr uint8_t VIRTIO_BLK_S_OK = 0;

    constexpr uint16_t VIRTQ_DESC_F_NEXT = 1u;
    constexpr uint16_t VIRTQ_DESC_F_WRITE = 2u;
    constexpr uint32_t VIRTIO_RESET_WAIT = 100000;
    constexpr uint32_t VIRTIO_QUEUE_PAGE_ORDER = 0;

    struct VirtioPciRegion {
        driver::pci::BarInfo bar;
        uint32_t offset;
        uint32_t length;
        bool found;
    };

    struct VirtioPciCaps {
        VirtioPciRegion common;
        VirtioPciRegion notify;
        VirtioPciRegion isr;
        VirtioPciRegion device;
        uint32_t notify_multiplier;
    };

    struct VirtioPciCommonCfg {
        volatile uint32_t device_feature_select;
        volatile uint32_t device_feature;
        volatile uint32_t driver_feature_select;
        volatile uint32_t driver_feature;
        volatile uint16_t msix_config;
        volatile uint16_t num_queues;
        volatile uint8_t device_status;
        volatile uint8_t config_generation;
        volatile uint16_t queue_select;
        volatile uint16_t queue_size;
        volatile uint16_t queue_msix_vector;
        volatile uint16_t queue_enable;
        volatile uint16_t queue_notify_off;
        volatile uint64_t queue_desc;
        volatile uint64_t queue_driver;
        volatile uint64_t queue_device;
    } __attribute__((packed));

    struct VirtioBlkConfig {
        volatile uint64_t capacity;
    } __attribute__((packed));

    driver::block::VirtioBlkDevice *g_irq_device = nullptr;

    void compiler_barrier() noexcept {
        asm volatile("" ::: "memory");
    }

    void spin_lock(volatile uint32_t *__lock) noexcept {
        while (__atomic_exchange_n(__lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
            while (__atomic_load_n(__lock, __ATOMIC_RELAXED) != 0u)
                asm volatile("pause" ::: "memory");
        }
    }

    void spin_unlock(volatile uint32_t *__lock) noexcept {
        __atomic_store_n(__lock, 0u, __ATOMIC_RELEASE);
    }

    driver::block::VirtioBlkStatus from_pci_status(driver::pci::Status __status) noexcept {
        switch (__status) {
            case driver::pci::Status::Ok:
                return driver::block::VirtioBlkStatus::Ok;
            case driver::pci::Status::UnsupportedContext:
                return driver::block::VirtioBlkStatus::UnsupportedContext;
            case driver::pci::Status::NoDevice:
                return driver::block::VirtioBlkStatus::NoDevice;
            case driver::pci::Status::UnsupportedBar:
            case driver::pci::Status::BadCapabilityList:
                return driver::block::VirtioBlkStatus::Unsupported;
            default:
                return driver::block::VirtioBlkStatus::PciError;
        }
    }

    driver::block::VirtioBlkStatus from_msix_status(driver::pci::msix::Status __status) noexcept {
        switch (__status) {
            case driver::pci::msix::Status::Ok:
                return driver::block::VirtioBlkStatus::Ok;
            case driver::pci::msix::Status::UnsupportedContext:
                return driver::block::VirtioBlkStatus::UnsupportedContext;
            case driver::pci::msix::Status::NoDevice:
                return driver::block::VirtioBlkStatus::NoDevice;
            case driver::pci::msix::Status::Unsupported:
                return driver::block::VirtioBlkStatus::Unsupported;
            case driver::pci::msix::Status::MappingFailed:
                return driver::block::VirtioBlkStatus::MappingFailed;
            default:
                return driver::block::VirtioBlkStatus::MsixFailed;
        }
    }

    bool range_within_bar(const driver::pci::BarInfo &__bar, uint32_t __offset, uint32_t __length) noexcept {
        if (__bar.kind != driver::pci::BarKind::Memory32 && __bar.kind != driver::pci::BarKind::Memory64)
            return false;
        if (__length == 0 || __bar.size == 0 || __offset >= __bar.size)
            return false;
        return (uint64_t)__length <= __bar.size - __offset;
    }

    driver::block::VirtioBlkStatus parse_region(driver::pci::FunctionAddress __address, uint8_t __cap_offset,
        VirtioPciRegion *__out, uint32_t *__notify_multiplier) noexcept {
        if (__out == nullptr)
            return driver::block::VirtioBlkStatus::InvalidArgument;

        uint8_t cfg_type = 0;
        uint8_t bar_index = 0;
        uint8_t cap_len = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
        driver::pci::Status pci_status = driver::pci::read_config8(__address, (uint8_t)(__cap_offset + 2), &cap_len);
        if (pci_status == driver::pci::Status::Ok)
            pci_status = driver::pci::read_config8(__address, (uint8_t)(__cap_offset + 3), &cfg_type);
        if (pci_status == driver::pci::Status::Ok)
            pci_status = driver::pci::read_config8(__address, (uint8_t)(__cap_offset + 4), &bar_index);
        if (pci_status == driver::pci::Status::Ok)
            pci_status = driver::pci::read_config32(__address, (uint8_t)(__cap_offset + 8), &offset);
        if (pci_status == driver::pci::Status::Ok)
            pci_status = driver::pci::read_config32(__address, (uint8_t)(__cap_offset + 12), &length);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);
        if (cap_len < 16 || bar_index >= driver::pci::BAR_COUNT)
            return driver::block::VirtioBlkStatus::Unsupported;

        driver::pci::BarInfo bar = {};
        pci_status = driver::pci::read_bar(__address, bar_index, &bar);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);
        if (!range_within_bar(bar, offset, length))
            return driver::block::VirtioBlkStatus::Unsupported;

        *__out = {bar, offset, length, true};
        if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG && __notify_multiplier != nullptr) {
            if (cap_len < 20)
                return driver::block::VirtioBlkStatus::Unsupported;
            return from_pci_status(
                driver::pci::read_config32(__address, (uint8_t)(__cap_offset + 16), __notify_multiplier));
        }
        return driver::block::VirtioBlkStatus::Ok;
    }

    driver::block::VirtioBlkStatus parse_virtio_caps(
        driver::pci::FunctionAddress __address, VirtioPciCaps *__out) noexcept {
        if (__out == nullptr)
            return driver::block::VirtioBlkStatus::InvalidArgument;
        *__out = {};

        driver::pci::Capability caps[driver::pci::MAX_CAPABILITIES] = {};
        uint8_t count = 0;
        driver::pci::Status pci_status =
            driver::pci::read_capabilities(__address, caps, driver::pci::MAX_CAPABILITIES, &count);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);

        for (uint8_t i = 0; i < count && i < driver::pci::MAX_CAPABILITIES; i++) {
            if (caps[i].id != VIRTIO_PCI_CAPABILITY_ID)
                continue;

            uint8_t cfg_type = 0;
            pci_status = driver::pci::read_config8(__address, (uint8_t)(caps[i].offset + 3), &cfg_type);
            if (pci_status != driver::pci::Status::Ok)
                return from_pci_status(pci_status);

            VirtioPciRegion *region = nullptr;
            if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG)
                region = &__out->common;
            else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG)
                region = &__out->notify;
            else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG)
                region = &__out->isr;
            else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG)
                region = &__out->device;
            if (region == nullptr)
                continue;

            const driver::block::VirtioBlkStatus status =
                parse_region(__address, caps[i].offset, region, &__out->notify_multiplier);
            if (status != driver::block::VirtioBlkStatus::Ok)
                return status;
        }

        return __out->common.found && __out->notify.found && __out->isr.found && __out->device.found ?
                   driver::block::VirtioBlkStatus::Ok :
                   driver::block::VirtioBlkStatus::Unsupported;
    }

    bigos::mm::DeviceMmioMapping map_region(const VirtioPciRegion &__region) noexcept {
        return bigos::mm::map_device_mmio(
            __region.bar.base + __region.offset, __region.length, bigos::mm::DeviceMmioCachePolicy::Uncached);
    }

    VirtioPciCommonCfg *common(driver::block::VirtioBlkDevice *__device) noexcept {
        return (VirtioPciCommonCfg *)__device->common_cfg;
    }

    void set_failed(driver::block::VirtioBlkDevice *__device) noexcept {
        if (__device != nullptr && __device->common_cfg != nullptr) {
            VirtioPciCommonCfg *cfg = common(__device);
            cfg->device_status = (uint8_t)(cfg->device_status | VIRTIO_STATUS_FAILED);
            compiler_barrier();
        }
    }

    driver::block::VirtioBlkStatus map_caps(
        driver::block::VirtioBlkDevice *__device, const VirtioPciCaps &__caps) noexcept {
        __device->common_mapping = map_region(__caps.common);
        __device->notify_mapping = map_region(__caps.notify);
        __device->isr_mapping = map_region(__caps.isr);
        __device->device_mapping = map_region(__caps.device);
        if (!__device->common_mapping.valid || !__device->notify_mapping.valid || !__device->isr_mapping.valid ||
            !__device->device_mapping.valid)
            return driver::block::VirtioBlkStatus::MappingFailed;

        __device->common_cfg = __device->common_mapping.vaddr;
        __device->notify_multiplier = __caps.notify_multiplier;
        __device->isr_cfg = (volatile uint8_t *)__device->isr_mapping.vaddr;
        __device->device_cfg = __device->device_mapping.vaddr;
        return driver::block::VirtioBlkStatus::Ok;
    }

    driver::block::VirtioBlkStatus negotiate_features(driver::block::VirtioBlkDevice *__device) noexcept {
        VirtioPciCommonCfg *cfg = common(__device);
        cfg->device_status = 0;
        compiler_barrier();
        for (uint32_t i = 0; i < VIRTIO_RESET_WAIT; i++) {
            if (cfg->device_status == 0)
                break;
            asm volatile("pause" ::: "memory");
        }
        if (cfg->device_status != 0)
            return driver::block::VirtioBlkStatus::DeviceError;

        cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
        compiler_barrier();
        cfg->device_status = (uint8_t)(cfg->device_status | VIRTIO_STATUS_DRIVER);
        compiler_barrier();

        cfg->device_feature_select = (uint32_t)(VIRTIO_F_VERSION_1_BIT / 32);
        compiler_barrier();
        if ((cfg->device_feature & (1u << (VIRTIO_F_VERSION_1_BIT % 32))) == 0)
            return driver::block::VirtioBlkStatus::FeatureNegotiationFailed;

        cfg->driver_feature_select = 0;
        cfg->driver_feature = 0;
        cfg->driver_feature_select = 1;
        cfg->driver_feature = 1u << (VIRTIO_F_VERSION_1_BIT % 32);
        compiler_barrier();
        cfg->device_status = (uint8_t)(cfg->device_status | VIRTIO_STATUS_FEATURES_OK);
        compiler_barrier();
        if ((cfg->device_status & VIRTIO_STATUS_FEATURES_OK) == 0)
            return driver::block::VirtioBlkStatus::FeatureNegotiationFailed;
        return driver::block::VirtioBlkStatus::Ok;
    }

    driver::block::VirtioBlkStatus alloc_dma(driver::block::VirtioBlkDevice *__device) noexcept {
        void *queue_phys = bigos::mm::__detail::alloc_physical_order(VIRTIO_QUEUE_PAGE_ORDER, 0);
        if (queue_phys == nullptr)
            return driver::block::VirtioBlkStatus::AllocationFailed;
        __device->queue_phys = (uint64_t)queue_phys;
        __device->queue_mem = (uint8_t *)bigos::mm::phys_to_direct(__device->queue_phys);
        if (__device->queue_mem == nullptr)
            return driver::block::VirtioBlkStatus::AllocationFailed;
        memset(__device->queue_mem, 0, 4096);

        uint8_t *cursor = __device->queue_mem;
        __device->desc = (driver::block::VirtqDesc *)cursor;
        cursor += sizeof(driver::block::VirtqDesc) * driver::block::VIRTIO_BLK_QUEUE_SIZE;
        __device->avail = (driver::block::VirtqAvail *)cursor;
        cursor += sizeof(driver::block::VirtqAvail);
        cursor = (uint8_t *)(((uint64_t)cursor + 3ull) & ~3ull);
        __device->used = (driver::block::VirtqUsed *)cursor;

        for (uint16_t i = 0; i < driver::block::VIRTIO_BLK_REQUEST_SLOTS; i++) {
            void *data_phys = bigos::mm::__detail::alloc_physical_order(0, 0);
            if (data_phys == nullptr)
                return driver::block::VirtioBlkStatus::AllocationFailed;
            const uint64_t base_phys = (uint64_t)data_phys;
            uint8_t *base = (uint8_t *)bigos::mm::phys_to_direct(base_phys);
            if (base == nullptr)
                return driver::block::VirtioBlkStatus::AllocationFailed;
            memset(base, 0, 4096);
            __device->slots[i].header_phys = base_phys;
            __device->slots[i].data_phys = base_phys + 64;
            __device->slots[i].status_phys = base_phys + 64 + driver::block::VIRTIO_BLK_SECTOR_SIZE;
            __device->slots[i].dma_header = (driver::block::VirtioBlkRequestHeader *)base;
            __device->slots[i].data = base + 64;
            __device->slots[i].dma_status = base + 64 + driver::block::VIRTIO_BLK_SECTOR_SIZE;
        }
        return driver::block::VirtioBlkStatus::Ok;
    }

    driver::block::VirtioBlkStatus configure_queue(driver::block::VirtioBlkDevice *__device) noexcept {
        VirtioPciCommonCfg *cfg = common(__device);
        cfg->queue_select = 0;
        compiler_barrier();
        if (cfg->queue_size < driver::block::VIRTIO_BLK_QUEUE_SIZE || cfg->num_queues == 0)
            return driver::block::VirtioBlkStatus::QueueConfigFailed;

        cfg->queue_size = driver::block::VIRTIO_BLK_QUEUE_SIZE;
        cfg->queue_desc = __device->queue_phys;
        cfg->queue_driver = __device->queue_phys + ((uint8_t *)__device->avail - __device->queue_mem);
        cfg->queue_device = __device->queue_phys + ((uint8_t *)__device->used - __device->queue_mem);
        cfg->queue_enable = 1;
        compiler_barrier();
        if (cfg->queue_enable != 1)
            return driver::block::VirtioBlkStatus::QueueConfigFailed;

        __device->queue_size = driver::block::VIRTIO_BLK_QUEUE_SIZE;
        __device->notify_offset = cfg->queue_notify_off;
        const uint64_t notify_byte_offset = (uint64_t)__device->notify_offset * __device->notify_multiplier;
        if (notify_byte_offset + sizeof(uint16_t) > __device->notify_mapping.length)
            return driver::block::VirtioBlkStatus::QueueConfigFailed;
        __device->notify = (volatile uint16_t *)((uint8_t *)__device->notify_mapping.vaddr + notify_byte_offset);
        return driver::block::VirtioBlkStatus::Ok;
    }

    driver::block::VirtioBlkStatus configure_msix(driver::block::VirtioBlkDevice *__device) noexcept {
        driver::pci::msix::Status status =
            driver::pci::msix::find_capability(__device->pci_address, &__device->msix_info);
        if (status != driver::pci::msix::Status::Ok)
            return from_msix_status(status);
        status = driver::pci::msix::set_function_mask(__device->msix_info, true);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::map_table_and_pba(__device->msix_info, &__device->msix_mapping);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::allocate_and_program_entry(
                __device->msix_mapping, 0, [](bigos::irq::InterruptFrame *) noexcept { driver::block::virtio_blk_irq(); },
                driver::irqchip::lapic::id(), &__device->completion_vector);
        if (status != driver::pci::msix::Status::Ok)
            return from_msix_status(status);

        VirtioPciCommonCfg *cfg = common(__device);
        cfg->queue_select = 0;
        cfg->queue_msix_vector = __device->completion_vector.entry_index;
        compiler_barrier();
        if (cfg->queue_msix_vector != __device->completion_vector.entry_index)
            return driver::block::VirtioBlkStatus::MsixFailed;

        status = driver::pci::msix::set_enable(__device->msix_info, true);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::set_entry_mask(
                __device->msix_mapping, __device->completion_vector.entry_index, false);
        if (status == driver::pci::msix::Status::Ok)
            status = driver::pci::msix::set_function_mask(__device->msix_info, false);
        if (status != driver::pci::msix::Status::Ok)
            return from_msix_status(status);
        __device->msix_enabled = true;
        return driver::block::VirtioBlkStatus::Ok;
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
                if (id.vendor_id == VIRTIO_PCI_VENDOR_ID &&
                    (id.device_id == VIRTIO_PCI_MODERN_BLK_DEVICE_ID ||
                        id.device_id == VIRTIO_PCI_TRANSITIONAL_BLK_DEVICE_ID)) {
                    *__out = address;
                    return true;
                }
            }
        }
        return false;
    }

    driver::block::BlockStatus virtio_read_sync(
        driver::block::BlockDevice *, uint64_t, uint32_t, void *, size_t) noexcept {
        return driver::block::BlockStatus::Unsupported;
    }

    driver::block::BlockStatus virtio_write_sync(
        driver::block::BlockDevice *, uint64_t, uint32_t, const void *, size_t) noexcept {
        return driver::block::BlockStatus::Unsupported;
    }

    uint16_t slot_head(uint16_t __slot) noexcept {
        return (uint16_t)(__slot * 3);
    }

    bigos::block_io::Status virtio_issue(driver::block::BlockDevice *__block, bigos::block_io::Request *__request,
        const bigos::block_io::CompletionToken *__token) noexcept {
        if (__block == nullptr || __block->context == nullptr || __request == nullptr || __token == nullptr)
            return bigos::block_io::Status::InvalidRequest;
        if (__request->sector_count != 1 || __request->buffer_len < driver::block::VIRTIO_BLK_SECTOR_SIZE)
            return bigos::block_io::Status::Unsupported;

        driver::block::VirtioBlkDevice *dev = (driver::block::VirtioBlkDevice *)__block->context;
        if (!dev->initialized || dev->notify == nullptr)
            return bigos::block_io::Status::DeviceNotReady;

        bigos::irq::InterruptGuard guard;
        spin_lock(&dev->lock);
        uint16_t slot = UINT16_MAX;
        for (uint16_t i = 0; i < driver::block::VIRTIO_BLK_REQUEST_SLOTS; i++) {
            if (!dev->slots[i].active) {
                slot = i;
                break;
            }
        }
        if (slot == UINT16_MAX) {
            spin_unlock(&dev->lock);
            return bigos::block_io::Status::QueueFull;
        }

        driver::block::VirtioBlkSlot &entry = dev->slots[slot];
        entry.active = true;
        entry.reading = __request->operation == bigos::block_io::Operation::Read;
        entry.request = __request;
        entry.token = *__token;
        entry.status = 0xff;
        entry.header.type = entry.reading ? VIRTIO_BLK_T_IN : VIRTIO_BLK_T_OUT;
        entry.header.reserved = 0;
        entry.header.sector = __request->lba;
        *entry.dma_header = entry.header;
        *entry.dma_status = 0xff;
        if (!entry.reading)
            memcpy(entry.data, __request->buffer, driver::block::VIRTIO_BLK_SECTOR_SIZE);

        const uint16_t head = slot_head(slot);
        dev->desc[head] = {entry.header_phys, sizeof(entry.header), VIRTQ_DESC_F_NEXT, (uint16_t)(head + 1)};
        dev->desc[head + 1] = {
            entry.data_phys,
            driver::block::VIRTIO_BLK_SECTOR_SIZE,
            (uint16_t)(VIRTQ_DESC_F_NEXT | (entry.reading ? VIRTQ_DESC_F_WRITE : 0)),
            (uint16_t)(head + 2),
        };
        dev->desc[head + 2] = {entry.status_phys, sizeof(entry.status), VIRTQ_DESC_F_WRITE, 0};
        compiler_barrier();
        dev->avail->ring[dev->avail_idx % dev->queue_size] = head;
        compiler_barrier();
        dev->avail_idx++;
        dev->avail->idx = dev->avail_idx;
        compiler_barrier();
        *dev->notify = 0;
        spin_unlock(&dev->lock);
        return bigos::block_io::Status::Success;
    }

    void finish_slot(driver::block::VirtioBlkDevice *__dev, uint16_t __slot, bigos::block_io::Status __status) noexcept {
        driver::block::VirtioBlkSlot &slot = __dev->slots[__slot];
        bigos::block_io::CompletionToken token = slot.token;
        if (__status == bigos::block_io::Status::Success && slot.reading && slot.request != nullptr)
            memcpy(slot.request->buffer, slot.data, driver::block::VIRTIO_BLK_SECTOR_SIZE);
        slot.active = false;
        slot.request = nullptr;
        slot.token = {};
        spin_unlock(&__dev->lock);
        (void)bigos::block_io::complete_from_irq(&token, __status);
        spin_lock(&__dev->lock);
    }

    bool bytes_equal(const uint8_t *__a, const uint8_t *__b, size_t __len) noexcept {
        for (size_t i = 0; i < __len; i++) {
            if (__a[i] != __b[i])
                return false;
        }
        return true;
    }

    bool virtio_cache_round_trip(driver::block::BlockDevice *__block) noexcept {
        if (__block == nullptr)
            return false;
        const uint64_t block_no = 2;
        const uint8_t pattern = 0x5a;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(__block, block_no);
        if (block == nullptr)
            return false;
        for (uint32_t i = 0; i < driver::block::VIRTIO_BLK_SECTOR_SIZE; i++)
            block->data[i] = (uint8_t)(pattern ^ i);
        bigos::bcache::mark_dirty(block);
        if (bigos::bcache::sync(block) != bigos::bcache::Status::Success) {
            bigos::bcache::put(block);
            return false;
        }
        bigos::bcache::put(block);
        if (bigos::bcache::invalidate_device(__block) != bigos::bcache::Status::Success)
            return false;
        block = bigos::bcache::get(__block, block_no);
        if (block == nullptr)
            return false;
        bool ok = true;
        for (uint32_t i = 0; i < driver::block::VIRTIO_BLK_SECTOR_SIZE; i++) {
            if (block->data[i] != (uint8_t)(pattern ^ i)) {
                ok = false;
                break;
            }
        }
        bigos::bcache::put(block);
        return ok;
    }

    bool virtio_cache_dirty_failure_retains_state(driver::block::BlockDevice *__block) noexcept {
        if (__block == nullptr)
            return false;
        const uint64_t block_no = 3;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(__block, block_no);
        if (block == nullptr)
            return false;
        block->data[0] ^= 0xff;
        bigos::bcache::mark_dirty(block);

        const uint64_t saved_total_sectors = __block->total_sectors;
        __block->total_sectors = block_no;
        const bigos::bcache::Status failed_sync = bigos::bcache::sync(block);
        const bool dirty_retained = failed_sync != bigos::bcache::Status::Success && block->dirty;
        __block->total_sectors = saved_total_sectors;

        const bigos::bcache::Status recovery_sync = bigos::bcache::sync(block);
        bigos::bcache::put(block);
        return dirty_retained && recovery_sync == bigos::bcache::Status::Success;
    }
}   // namespace

NAMESPACE_DRIVER_BEG
namespace block {
    VirtioBlkStatus virtio_blk_init(VirtioBlkDevice *__device) noexcept {
        if (__device == nullptr)
            return VirtioBlkStatus::InvalidArgument;
        if (!driver::pci::context_allows_config_access() || !driver::pci::msix::context_allows_msix_config())
            return VirtioBlkStatus::UnsupportedContext;

        *__device = {};
        if (!find_device(&__device->pci_address))
            return VirtioBlkStatus::NoDevice;

        uint16_t command = 0;
        driver::pci::Status pci_status = driver::pci::read_config16(__device->pci_address, PCI_REG_COMMAND, &command);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);
        command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
        command &= (uint16_t)~PCI_COMMAND_IO_SPACE;
        pci_status = driver::pci::write_config16(__device->pci_address, PCI_REG_COMMAND, command);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);

        VirtioPciCaps caps = {};
        VirtioBlkStatus status = parse_virtio_caps(__device->pci_address, &caps);
        if (status != VirtioBlkStatus::Ok)
            return status;
        status = map_caps(__device, caps);
        if (status != VirtioBlkStatus::Ok)
            return status;
        status = negotiate_features(__device);
        if (status != VirtioBlkStatus::Ok) {
            set_failed(__device);
            return status;
        }
        status = alloc_dma(__device);
        if (status == VirtioBlkStatus::Ok)
            status = configure_queue(__device);
        if (status == VirtioBlkStatus::Ok)
            status = configure_msix(__device);
        if (status != VirtioBlkStatus::Ok) {
            set_failed(__device);
            return status;
        }

        VirtioBlkConfig *blk_cfg = (VirtioBlkConfig *)__device->device_cfg;
        __device->capacity_sectors = blk_cfg->capacity;
        VirtioPciCommonCfg *cfg = common(__device);
        cfg->device_status = (uint8_t)(cfg->device_status | VIRTIO_STATUS_DRIVER_OK);
        compiler_barrier();

        __device->block.sector_size = VIRTIO_BLK_SECTOR_SIZE;
        __device->block.total_sectors = __device->capacity_sectors;
        __device->block.context = __device;
        __device->block.read_impl = &virtio_read_sync;
        __device->block.write_impl = &virtio_write_sync;
        __device->block.issue_impl = &virtio_issue;
        __device->initialized = true;
        g_irq_device = __device;
        bigos::serial_puts("BIGOS_VIRTIO_BLK_PUBLISHED\n");
        return VirtioBlkStatus::Ok;
    }

    void virtio_blk_irq() noexcept {
        VirtioBlkDevice *dev = g_irq_device;
        if (dev == nullptr || !dev->initialized)
            return;

        spin_lock(&dev->lock);
        const uint16_t used_idx = dev->used->idx;
        while (dev->last_used_idx != used_idx) {
            const VirtqUsedElem elem = dev->used->ring[dev->last_used_idx % dev->queue_size];
            dev->last_used_idx++;
            if (elem.id % 3 != 0) {
                bigos::serial_puts("BIGOS_VIRTIO_BLK_COMPLETION_REJECTED descriptor\n");
                continue;
            }
            const uint16_t slot = (uint16_t)(elem.id / 3);
            if (slot >= VIRTIO_BLK_REQUEST_SLOTS || !dev->slots[slot].active) {
                bigos::serial_puts("BIGOS_VIRTIO_BLK_COMPLETION_REJECTED slot\n");
                continue;
            }
            const bigos::block_io::Status final_status =
                *dev->slots[slot].dma_status == VIRTIO_BLK_S_OK ? bigos::block_io::Status::Success :
                                                                  bigos::block_io::Status::DeviceError;
            finish_slot(dev, slot, final_status);
        }
        spin_unlock(&dev->lock);
    }

    const char *virtio_blk_status_name(VirtioBlkStatus __status) noexcept {
        switch (__status) {
            case VirtioBlkStatus::Ok:
                return "ok";
            case VirtioBlkStatus::InvalidArgument:
                return "invalid-argument";
            case VirtioBlkStatus::UnsupportedContext:
                return "unsupported-context";
            case VirtioBlkStatus::NoDevice:
                return "no-device";
            case VirtioBlkStatus::Unsupported:
                return "unsupported";
            case VirtioBlkStatus::PciError:
                return "pci-error";
            case VirtioBlkStatus::MappingFailed:
                return "mapping-failed";
            case VirtioBlkStatus::AllocationFailed:
                return "allocation-failed";
            case VirtioBlkStatus::FeatureNegotiationFailed:
                return "feature-negotiation-failed";
            case VirtioBlkStatus::QueueConfigFailed:
                return "queue-config-failed";
            case VirtioBlkStatus::MsixFailed:
                return "msix-failed";
            case VirtioBlkStatus::DeviceError:
                return "device-error";
            default:
                return "unknown";
        }
    }

#if defined(BIGOS_VIRTIO_BLK_SMOKE) || defined(BIGOS_MODERN_STORAGE_BACKEND_SMOKE)
    bool virtio_blk_smoke() noexcept {
        const bigos::device::Status probe_status = bigos::device::probe(bigos::device::DeviceClass::Block,
            bigos::device::DeviceRole::VirtioBlkValidationBlock, bigos::device::ProbeContext::OrdinaryBlockable);
        if (probe_status != bigos::device::Status::Success && probe_status != bigos::device::Status::ProbeFailed) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_SKIPPED probe-context\n");
            return true;
        }
        driver::block::BlockDevice *block =
            bigos::device::block(bigos::device::DeviceRole::VirtioBlkValidationBlock);
        if (block == nullptr) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_SKIPPED unavailable\n");
            return true;
        }

        uint8_t write_buf[VIRTIO_BLK_SECTOR_SIZE] = {};
        uint8_t read_buf[VIRTIO_BLK_SECTOR_SIZE] = {};
        for (uint32_t i = 0; i < VIRTIO_BLK_SECTOR_SIZE; i++)
            write_buf[i] = (uint8_t)(0xa0u ^ i);

        bigos::block_io::Status status = bigos::block_io::write_sync(block, 1, 1, write_buf, sizeof(write_buf));
        if (status != bigos::block_io::Status::Success) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_FAILED write status=");
            bigos::serial_puts(bigos::block_io::status_name(status));
            bigos::serial_puts("\n");
            return false;
        }
        status = bigos::block_io::read_sync(block, 1, 1, read_buf, sizeof(read_buf));
        if (status != bigos::block_io::Status::Success || !bytes_equal(write_buf, read_buf, sizeof(write_buf))) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_FAILED readback\n");
            return false;
        }
        if (!virtio_cache_round_trip(block)) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_FAILED cache-round-trip\n");
            return false;
        }
        if (!virtio_cache_dirty_failure_retains_state(block)) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_FAILED dirty-failure\n");
            return false;
        }

        bigos::block_io::Request timeout = {};
        timeout.device = block;
        timeout.operation = bigos::block_io::Operation::Read;
        timeout.lba = block->total_sectors;
        timeout.sector_count = 1;
        timeout.buffer = read_buf;
        timeout.buffer_len = sizeof(read_buf);
        timeout.queue_slot = UINT32_MAX;
        status = bigos::block_io::submit_sync(&timeout);
        if (status == bigos::block_io::Status::Success) {
            bigos::serial_puts("BIGOS_VIRTIO_BLK_FAILED error-path\n");
            return false;
        }

        bigos::serial_puts("BIGOS_VIRTIO_BLK_PASSED\n");
#ifdef BIGOS_MODERN_STORAGE_BACKEND_SMOKE
        bigos::serial_puts("BIGOS_MODERN_STORAGE_BACKEND_PASSED\n");
#endif
        return true;
    }
#endif
}   // namespace block
NAMESPACE_DRIVER_END
