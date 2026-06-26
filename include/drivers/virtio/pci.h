#ifndef _DRIVERS_VIRTIO_PCI_H
#define _DRIVERS_VIRTIO_PCI_H

#include <bigos/memory.h>
#include <bigos/types.h>
#include <drivers/pci/config.h>

NAMESPACE_DRIVER_BEG
namespace virtio {
    constexpr uint16_t PCI_VENDOR_ID = 0x1af4;
    constexpr uint16_t PCI_TRANSITIONAL_NET_DEVICE_ID = 0x1000;
    constexpr uint16_t PCI_TRANSITIONAL_BLK_DEVICE_ID = 0x1001;
    constexpr uint16_t PCI_MODERN_NET_DEVICE_ID = 0x1041;
    constexpr uint16_t PCI_MODERN_BLK_DEVICE_ID = 0x1042;

    constexpr uint8_t PCI_CAPABILITY_ID = 0x09;
    constexpr uint8_t PCI_CAP_COMMON_CFG = 1;
    constexpr uint8_t PCI_CAP_NOTIFY_CFG = 2;
    constexpr uint8_t PCI_CAP_ISR_CFG = 3;
    constexpr uint8_t PCI_CAP_DEVICE_CFG = 4;

    constexpr uint8_t PCI_REG_COMMAND = 0x04;
    constexpr uint16_t PCI_COMMAND_IO_SPACE = 1u << 0;
    constexpr uint16_t PCI_COMMAND_MEMORY_SPACE = 1u << 1;
    constexpr uint16_t PCI_COMMAND_BUS_MASTER = 1u << 2;

    constexpr uint64_t F_VERSION_1_BIT = 32;

    constexpr uint8_t STATUS_ACKNOWLEDGE = 1u;
    constexpr uint8_t STATUS_DRIVER = 2u;
    constexpr uint8_t STATUS_DRIVER_OK = 4u;
    constexpr uint8_t STATUS_FEATURES_OK = 8u;
    constexpr uint8_t STATUS_FAILED = 128u;

    constexpr uint16_t VIRTQ_DESC_F_NEXT = 1u;
    constexpr uint16_t VIRTQ_DESC_F_WRITE = 2u;

    enum class Status : uint8_t {
        Ok,
        InvalidArgument,
        UnsupportedContext,
        NoDevice,
        Unsupported,
        PciError,
        MappingFailed,
        FeatureNegotiationFailed,
        QueueConfigFailed,
        DeviceError,
    };

    struct PciRegion {
        driver::pci::BarInfo bar;
        uint32_t offset;
        uint32_t length;
        bool found;
    };

    struct PciCaps {
        PciRegion common;
        PciRegion notify;
        PciRegion isr;
        PciRegion device;
        uint32_t notify_multiplier;
    };

    struct CommonCfg {
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

    struct VirtqDesc {
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        uint16_t next;
    } __attribute__((packed));

    struct VirtqUsedElem {
        uint32_t id;
        uint32_t len;
    } __attribute__((packed));

    struct Transport {
        driver::pci::FunctionAddress pci_address;
        bigos::mm::DeviceMmioMapping common_mapping;
        bigos::mm::DeviceMmioMapping notify_mapping;
        bigos::mm::DeviceMmioMapping isr_mapping;
        bigos::mm::DeviceMmioMapping device_mapping;
        volatile void *common_cfg;
        volatile uint8_t *isr_cfg;
        volatile void *device_cfg;
        uint32_t notify_multiplier;
    };

    struct QueueRef {
        uint16_t index;
        uint16_t size;
        uint16_t notify_offset;
        volatile uint16_t *notify;
    };

    void compiler_barrier() noexcept;
    Status from_pci_status(driver::pci::Status __status) noexcept;
    Status enable_modern_pci_command(driver::pci::FunctionAddress __address) noexcept;
    Status parse_modern_pci_caps(driver::pci::FunctionAddress __address, PciCaps *__out) noexcept;
    bigos::mm::DeviceMmioMapping map_region(const PciRegion &__region) noexcept;
    Status map_caps(Transport *__transport, const PciCaps &__caps) noexcept;
    CommonCfg *common(Transport *__transport) noexcept;
    void set_failed(Transport *__transport) noexcept;
    Status reset_and_start_driver(Transport *__transport) noexcept;
    bool read_device_feature_bit(Transport *__transport, uint64_t __bit) noexcept;
    void write_driver_features(Transport *__transport, uint32_t __select, uint32_t __features) noexcept;
    Status accept_features(Transport *__transport) noexcept;
    void set_driver_ok(Transport *__transport) noexcept;
    Status configure_split_queue(Transport *__transport, uint16_t __queue_index, uint16_t __queue_size,
        uint64_t __desc_phys, uint64_t __avail_phys, uint64_t __used_phys, QueueRef *__out) noexcept;
    Status set_queue_msix_vector(Transport *__transport, uint16_t __queue_index, uint16_t __vector) noexcept;
    void notify_queue(const QueueRef &__queue) noexcept;
    const char *status_name(Status __status) noexcept;
}   // namespace virtio
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_VIRTIO_PCI_H
