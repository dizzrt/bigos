#ifndef _BIG_DRIVERS_PCI_CONFIG_H
#define _BIG_DRIVERS_PCI_CONFIG_H

#include <bigos/types.h>

namespace driver::pci {
    constexpr uint16_t INVALID_VENDOR_ID = 0xffff;
    constexpr uint8_t MAX_CAPABILITIES = 48;
    constexpr uint8_t BAR_COUNT = 6;

    struct FunctionAddress {
        uint8_t bus;
        uint8_t device;
        uint8_t function;
    };

    enum class Status : uint8_t {
        Ok,
        InvalidArgument,
        UnsupportedContext,
        NoDevice,
        BadCapabilityList,
        UnsupportedBar,
    };

    struct DeviceId {
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t class_code;
        uint8_t subclass;
        uint8_t prog_if;
        uint8_t header_type;
        uint16_t status;
    };

    struct Capability {
        uint8_t id;
        uint8_t offset;
    };

    enum class BarKind : uint8_t {
        None,
        Io,
        Memory32,
        Memory64,
    };

    struct BarInfo {
        uint8_t index;
        BarKind kind;
        bool prefetchable;
        uint64_t base;
        uint64_t size;
        uint32_t raw_low;
        uint32_t raw_high;
    };

    bool context_allows_config_access() noexcept;
    Status read_config32(FunctionAddress __address, uint8_t __offset, uint32_t *__out) noexcept;
    Status read_config16(FunctionAddress __address, uint8_t __offset, uint16_t *__out) noexcept;
    Status read_config8(FunctionAddress __address, uint8_t __offset, uint8_t *__out) noexcept;
    Status write_config32(FunctionAddress __address, uint8_t __offset, uint32_t __value) noexcept;
    Status write_config16(FunctionAddress __address, uint8_t __offset, uint16_t __value) noexcept;
    Status probe_device(FunctionAddress __address, DeviceId *__out) noexcept;
    Status read_capabilities(FunctionAddress __address, Capability *__out, uint8_t __capacity, uint8_t *__count) noexcept;
    Status read_bar(FunctionAddress __address, uint8_t __bar_index, BarInfo *__out) noexcept;
    const char *status_name(Status __status) noexcept;
}   // namespace driver::pci

#endif   // _BIG_DRIVERS_PCI_CONFIG_H
