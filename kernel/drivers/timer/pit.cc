#include <drivers/timer/pit.h>

#include <bigos/io.h>

NAMESPACE_DRIVER_BEG
namespace timer::pit {
    constexpr uint8_t LATCH_CHANNEL0_COMMAND = 0x00;

    void init_channel0() noexcept {
        bigos::outb(COMMAND_PORT, CHANNEL0_RATE_COMMAND);
        bigos::outb(CHANNEL0_DATA_PORT, (uint8_t)(CHANNEL0_DIVISOR & 0xff));
        bigos::outb(CHANNEL0_DATA_PORT, (uint8_t)((CHANNEL0_DIVISOR >> 8) & 0xff));
    }

    uint16_t read_channel0_counter() noexcept {
        bigos::outb(COMMAND_PORT, LATCH_CHANNEL0_COMMAND);
        const uint8_t lo = bigos::inb(CHANNEL0_DATA_PORT);
        const uint8_t hi = bigos::inb(CHANNEL0_DATA_PORT);
        return (uint16_t)((uint16_t)hi << 8 | lo);
    }
}   // namespace timer::pit
NAMESPACE_DRIVER_END
