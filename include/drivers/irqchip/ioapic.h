#ifndef _BIG_IOAPIC_H
#define _BIG_IOAPIC_H

#include <bigos/types.h>

NAMESPACE_DRIVER_BEG
namespace irqchip::ioapic {
    enum class Status : uint8_t {
        Uninitialized,
        DirectMapUnavailable,
        Ready,
        InvalidInput,
    };

    enum class TriggerMode : uint8_t {
        Edge,
        Level,
    };

    enum class Polarity : uint8_t {
        ActiveHigh,
        ActiveLow,
    };

    struct RedirectionConfig {
        uint8_t irq;
        uint8_t vector;
        uint32_t destination_apic_id;
        bool masked;
        TriggerMode trigger;
        Polarity polarity;
    };

    bool init(uint32_t __phys_address) noexcept;
    Status status() noexcept;
    uint32_t id() noexcept;
    uint32_t version() noexcept;
    bool route_irq(const RedirectionConfig &__config) noexcept;
    bool route_irq(uint8_t __irq, uint8_t __vector, uint32_t __destination_apic_id, bool __masked) noexcept;
}   // namespace irqchip::ioapic
NAMESPACE_DRIVER_END

#endif   // _BIG_IOAPIC_H
