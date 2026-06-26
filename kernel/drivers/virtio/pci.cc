#include <drivers/virtio/pci.h>

NAMESPACE_DRIVER_BEG
namespace virtio {
    namespace {
        constexpr uint32_t RESET_WAIT_ITERATIONS = 100000;

        bool range_within_bar(const driver::pci::BarInfo &__bar, uint32_t __offset, uint32_t __length) noexcept {
            if (__bar.kind != driver::pci::BarKind::Memory32 && __bar.kind != driver::pci::BarKind::Memory64)
                return false;
            if (__length == 0 || __bar.size == 0 || __offset >= __bar.size)
                return false;
            return (uint64_t)__length <= __bar.size - __offset;
        }

        Status parse_region(driver::pci::FunctionAddress __address, uint8_t __cap_offset, PciRegion *__out,
            uint32_t *__notify_multiplier) noexcept {
            if (__out == nullptr)
                return Status::InvalidArgument;

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
                return Status::Unsupported;

            driver::pci::BarInfo bar = {};
            pci_status = driver::pci::read_bar(__address, bar_index, &bar);
            if (pci_status != driver::pci::Status::Ok)
                return from_pci_status(pci_status);
            if (!range_within_bar(bar, offset, length))
                return Status::Unsupported;

            *__out = {bar, offset, length, true};
            if (cfg_type == PCI_CAP_NOTIFY_CFG && __notify_multiplier != nullptr) {
                if (cap_len < 20)
                    return Status::Unsupported;
                return from_pci_status(
                    driver::pci::read_config32(__address, (uint8_t)(__cap_offset + 16), __notify_multiplier));
            }
            return Status::Ok;
        }
    }   // namespace

    void compiler_barrier() noexcept {
        asm volatile("" ::: "memory");
    }

    Status from_pci_status(driver::pci::Status __status) noexcept {
        switch (__status) {
            case driver::pci::Status::Ok:
                return Status::Ok;
            case driver::pci::Status::UnsupportedContext:
                return Status::UnsupportedContext;
            case driver::pci::Status::NoDevice:
                return Status::NoDevice;
            case driver::pci::Status::UnsupportedBar:
            case driver::pci::Status::BadCapabilityList:
                return Status::Unsupported;
            default:
                return Status::PciError;
        }
    }

    Status enable_modern_pci_command(driver::pci::FunctionAddress __address) noexcept {
        uint16_t command = 0;
        driver::pci::Status pci_status = driver::pci::read_config16(__address, PCI_REG_COMMAND, &command);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);
        command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
        command &= (uint16_t)~PCI_COMMAND_IO_SPACE;
        pci_status = driver::pci::write_config16(__address, PCI_REG_COMMAND, command);
        return from_pci_status(pci_status);
    }

    Status parse_modern_pci_caps(driver::pci::FunctionAddress __address, PciCaps *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        *__out = {};

        driver::pci::Capability caps[driver::pci::MAX_CAPABILITIES] = {};
        uint8_t count = 0;
        driver::pci::Status pci_status =
            driver::pci::read_capabilities(__address, caps, driver::pci::MAX_CAPABILITIES, &count);
        if (pci_status != driver::pci::Status::Ok)
            return from_pci_status(pci_status);

        for (uint8_t i = 0; i < count && i < driver::pci::MAX_CAPABILITIES; i++) {
            if (caps[i].id != PCI_CAPABILITY_ID)
                continue;

            uint8_t cfg_type = 0;
            pci_status = driver::pci::read_config8(__address, (uint8_t)(caps[i].offset + 3), &cfg_type);
            if (pci_status != driver::pci::Status::Ok)
                return from_pci_status(pci_status);

            PciRegion *region = nullptr;
            if (cfg_type == PCI_CAP_COMMON_CFG)
                region = &__out->common;
            else if (cfg_type == PCI_CAP_NOTIFY_CFG)
                region = &__out->notify;
            else if (cfg_type == PCI_CAP_ISR_CFG)
                region = &__out->isr;
            else if (cfg_type == PCI_CAP_DEVICE_CFG)
                region = &__out->device;
            if (region == nullptr)
                continue;

            const Status status = parse_region(__address, caps[i].offset, region, &__out->notify_multiplier);
            if (status != Status::Ok)
                return status;
        }

        return __out->common.found && __out->notify.found && __out->isr.found && __out->device.found ?
                   Status::Ok :
                   Status::Unsupported;
    }

    bigos::mm::DeviceMmioMapping map_region(const PciRegion &__region) noexcept {
        return bigos::mm::map_device_mmio(
            __region.bar.base + __region.offset, __region.length, bigos::mm::DeviceMmioCachePolicy::Uncached);
    }

    Status map_caps(Transport *__transport, const PciCaps &__caps) noexcept {
        if (__transport == nullptr)
            return Status::InvalidArgument;
        __transport->common_mapping = map_region(__caps.common);
        __transport->notify_mapping = map_region(__caps.notify);
        __transport->isr_mapping = map_region(__caps.isr);
        __transport->device_mapping = map_region(__caps.device);
        if (!__transport->common_mapping.valid || !__transport->notify_mapping.valid ||
            !__transport->isr_mapping.valid || !__transport->device_mapping.valid)
            return Status::MappingFailed;

        __transport->common_cfg = __transport->common_mapping.vaddr;
        __transport->notify_multiplier = __caps.notify_multiplier;
        __transport->isr_cfg = (volatile uint8_t *)__transport->isr_mapping.vaddr;
        __transport->device_cfg = __transport->device_mapping.vaddr;
        return Status::Ok;
    }

    CommonCfg *common(Transport *__transport) noexcept {
        if (__transport == nullptr)
            return nullptr;
        return (CommonCfg *)__transport->common_cfg;
    }

    void set_failed(Transport *__transport) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return;
        cfg->device_status = (uint8_t)(cfg->device_status | STATUS_FAILED);
        compiler_barrier();
    }

    Status reset_and_start_driver(Transport *__transport) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return Status::InvalidArgument;
        cfg->device_status = 0;
        compiler_barrier();
        for (uint32_t i = 0; i < RESET_WAIT_ITERATIONS; i++) {
            if (cfg->device_status == 0)
                break;
            asm volatile("pause" ::: "memory");
        }
        if (cfg->device_status != 0)
            return Status::DeviceError;
        cfg->device_status = STATUS_ACKNOWLEDGE;
        compiler_barrier();
        cfg->device_status = (uint8_t)(cfg->device_status | STATUS_DRIVER);
        compiler_barrier();
        return Status::Ok;
    }

    bool read_device_feature_bit(Transport *__transport, uint64_t __bit) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return false;
        cfg->device_feature_select = (uint32_t)(__bit / 32);
        compiler_barrier();
        return (cfg->device_feature & (1u << (__bit % 32))) != 0;
    }

    void write_driver_features(Transport *__transport, uint32_t __select, uint32_t __features) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return;
        cfg->driver_feature_select = __select;
        cfg->driver_feature = __features;
        compiler_barrier();
    }

    Status accept_features(Transport *__transport) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return Status::InvalidArgument;
        cfg->device_status = (uint8_t)(cfg->device_status | STATUS_FEATURES_OK);
        compiler_barrier();
        return (cfg->device_status & STATUS_FEATURES_OK) != 0 ? Status::Ok : Status::FeatureNegotiationFailed;
    }

    void set_driver_ok(Transport *__transport) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return;
        cfg->device_status = (uint8_t)(cfg->device_status | STATUS_DRIVER_OK);
        compiler_barrier();
    }

    Status configure_split_queue(Transport *__transport, uint16_t __queue_index, uint16_t __queue_size,
        uint64_t __desc_phys, uint64_t __avail_phys, uint64_t __used_phys, QueueRef *__out) noexcept {
        if (__transport == nullptr || __out == nullptr || __queue_size == 0)
            return Status::InvalidArgument;
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return Status::InvalidArgument;
        if (__queue_index >= cfg->num_queues)
            return Status::QueueConfigFailed;

        cfg->queue_select = __queue_index;
        compiler_barrier();
        if (cfg->queue_size < __queue_size)
            return Status::QueueConfigFailed;

        cfg->queue_size = __queue_size;
        cfg->queue_desc = __desc_phys;
        cfg->queue_driver = __avail_phys;
        cfg->queue_device = __used_phys;
        cfg->queue_enable = 1;
        compiler_barrier();
        if (cfg->queue_enable != 1)
            return Status::QueueConfigFailed;

        const uint16_t notify_offset = cfg->queue_notify_off;
        const uint64_t notify_byte_offset = (uint64_t)notify_offset * __transport->notify_multiplier;
        if (notify_byte_offset + sizeof(uint16_t) > __transport->notify_mapping.length)
            return Status::QueueConfigFailed;

        __out->index = __queue_index;
        __out->size = __queue_size;
        __out->notify_offset = notify_offset;
        __out->notify = (volatile uint16_t *)((uint8_t *)__transport->notify_mapping.vaddr + notify_byte_offset);
        return Status::Ok;
    }

    Status set_queue_msix_vector(Transport *__transport, uint16_t __queue_index, uint16_t __vector) noexcept {
        CommonCfg *cfg = common(__transport);
        if (cfg == nullptr)
            return Status::InvalidArgument;
        if (__queue_index >= cfg->num_queues)
            return Status::QueueConfigFailed;
        cfg->queue_select = __queue_index;
        cfg->queue_msix_vector = __vector;
        compiler_barrier();
        return cfg->queue_msix_vector == __vector ? Status::Ok : Status::QueueConfigFailed;
    }

    void notify_queue(const QueueRef &__queue) noexcept {
        compiler_barrier();
        if (__queue.notify != nullptr)
            *__queue.notify = __queue.index;
    }

    const char *status_name(Status __status) noexcept {
        switch (__status) {
            case Status::Ok:
                return "ok";
            case Status::InvalidArgument:
                return "invalid-argument";
            case Status::UnsupportedContext:
                return "unsupported-context";
            case Status::NoDevice:
                return "no-device";
            case Status::Unsupported:
                return "unsupported";
            case Status::PciError:
                return "pci-error";
            case Status::MappingFailed:
                return "mapping-failed";
            case Status::FeatureNegotiationFailed:
                return "feature-negotiation-failed";
            case Status::QueueConfigFailed:
                return "queue-config-failed";
            case Status::DeviceError:
                return "device-error";
            default:
                return "unknown";
        }
    }
}   // namespace virtio
NAMESPACE_DRIVER_END
