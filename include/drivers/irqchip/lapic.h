#ifndef _BIG_LAPIC_H
#define _BIG_LAPIC_H

#include <bigos/types.h>

NAMESPACE_DRIVER_BEG
namespace irqchip::lapic {
    enum class Status : uint8_t {
        Uninitialized,
        Unavailable,
        DirectMapUnavailable,
        Enabled,
        SendTimeout,
    };

    bool supported() noexcept;
    bool init() noexcept;
    bool enable_x2apic() noexcept;
    Status status() noexcept;
    uint32_t id() noexcept;
    uint64_t base_phys() noexcept;
    void send_eoi() noexcept;
    bool send_init(uint32_t __apic_id) noexcept;
    bool send_sipi(uint32_t __apic_id, uint8_t __vector) noexcept;
    bool calibrate_timer_with_pit(uint32_t *__initial_count) noexcept;
    bool configure_timer(uint8_t __vector, uint32_t __initial_count, bool __periodic) noexcept;
    uint32_t timer_initial_count() noexcept;
    void disable_timer() noexcept;
}   // namespace irqchip::lapic
NAMESPACE_DRIVER_END

#endif   // _BIG_LAPIC_H
