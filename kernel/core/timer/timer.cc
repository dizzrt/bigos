#include <bigos/timer.h>

#include <bigos/io.h>
#include <bigos/sched.h>

NAMESPACE_BIGOS_BEG
namespace timer::__detail {
    volatile tick_t g_ticks = 0;
}   // namespace timer::__detail

namespace timer {
    void on_tick() noexcept {
        ++__detail::g_ticks;
    }

    tick_t ticks() noexcept {
        return __detail::g_ticks;
    }

    void mdelay(uint64_t milliseconds) noexcept {
        if (milliseconds == 0)
            return;

        const tick_t start = ticks();
        const tick_t wait_ticks = (milliseconds * TIMER_HZ + 999) / 1000;
        const tick_t target = start + (wait_ticks == 0 ? 1 : wait_ticks);

        while (ticks() < target) {
            asm volatile("pause");
        }
    }

    int sleep_for(tick_t __ticks) noexcept {
        return bigos::sched::sleep_for(__ticks);
    }
}   // namespace timer
NAMESPACE_BIGOS_END
