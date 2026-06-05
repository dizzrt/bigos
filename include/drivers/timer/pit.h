#ifndef _BIG_PIT_H
#define _BIG_PIT_H

#include <bigos/types.h>

NAMESPACE_DRIVER_BEG
namespace timer::pit {
    constexpr uint16_t CHANNEL0_DATA_PORT = 0x40;
    constexpr uint16_t COMMAND_PORT = 0x43;
    constexpr uint32_t BASE_FREQUENCY_HZ = 1193182;
    constexpr uint32_t TIMER_HZ = 100;
    constexpr uint16_t CHANNEL0_DIVISOR = (uint16_t)(BASE_FREQUENCY_HZ / TIMER_HZ);
    constexpr uint8_t CHANNEL0_RATE_COMMAND = 0x36;

    void init_channel0() noexcept;
}   // namespace timer::pit
NAMESPACE_DRIVER_END

#endif   // _BIG_PIT_H
