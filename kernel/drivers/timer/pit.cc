#include <drivers/timer/pit.h>

#include <bigos/io.h>

NAMESPACE_DRIVER_BEG
namespace timer::pit {
    void init_channel0() noexcept {
        bigos::outb(COMMAND_PORT, CHANNEL0_RATE_COMMAND);
        bigos::outb(CHANNEL0_DATA_PORT, (uint8_t)(CHANNEL0_DIVISOR & 0xff));
        bigos::outb(CHANNEL0_DATA_PORT, (uint8_t)((CHANNEL0_DIVISOR >> 8) & 0xff));
    }
}   // namespace timer::pit
NAMESPACE_DRIVER_END
