#ifndef _BIG_TIMER_H
#define _BIG_TIMER_H

#include <bigos/types.h>
#include <drivers/timer/pit.h>

NAMESPACE_BIGOS_BEG
namespace timer {
    using tick_t = uint64_t;

    constexpr uint32_t TIMER_HZ = driver::timer::pit::TIMER_HZ;

    namespace __detail {
        extern volatile tick_t g_ticks;
    }   // namespace __detail

    tick_t ticks() noexcept;

    // Busy-waits on the early PIT tick counter. This is not scheduler sleep,
    // does not yield or block, and only provides coarse single-core timing.
    void mdelay(uint64_t milliseconds) noexcept;
}   // namespace timer
NAMESPACE_BIGOS_END

#endif   // _BIG_TIMER_H
