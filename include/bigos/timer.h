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

    // IRQ-context only. Called from the timer IRQ0 handler to advance the
    // monotonic kernel tick. It only increments the tick counter: it does not
    // allocate, block, do IO, call kprintf, or send i8259 EOI. Do not call from
    // ordinary non-interrupt kernel code paths.
    void on_tick() noexcept;

    // Context-agnostic read. Returns a monotonic tick snapshot valid under the
    // current single-core early-kernel model. It makes no guarantee about SMP
    // coherence or high-resolution timing.
    tick_t ticks() noexcept;

    // Non-interrupt context only. Must run with maskable interrupts enabled so
    // that IRQ0 keeps advancing ticks. Busy-waits on the early PIT tick counter:
    // this is not scheduler sleep, does not yield or block, and only provides
    // coarse single-core timing. Calling it with interrupts disabled or inside an
    // IRQ handler busy-waits forever.
    void mdelay(uint64_t milliseconds) noexcept;
}   // namespace timer
NAMESPACE_BIGOS_END

#endif   // _BIG_TIMER_H
