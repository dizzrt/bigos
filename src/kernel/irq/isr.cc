#include <irq/isr.h>

#include <bigos/keyboard.h>
#include <bigos/io.h>
#include <bigos/sched.h>
#include <bigos/timer.h>
#include <bigos/types.h>
#include <drivers/irqchip/i8259.h>
#include <drivers/timer/pit.h>

NAMESPACE_BIGOS_BEG
namespace irq::isr {
    namespace __detail {
        constexpr uint16_t PS2_KEYBOARD_DATA_PORT = 0x60;
        constexpr bigos::timer::tick_t TIMER_SMOKE_MARKER_LIMIT = 3;

        implement_isr(timer) {
            (void)__frame;
            // Advance the monotonic tick through the timer-owned IRQ-context-safe
            // API. The IRQ layer does not mutate timer-internal tick state directly,
            // and does not send i8259 EOI here; EOI stays owned by irq_dispatch.
            bigos::timer::on_tick();

            // Bounded IRQ-context-safe hook: records reschedule intent only.
            // Stage 4 performs no IRQ-return preemption, no allocation, and no
            // thread switch from this path.
            bigos::sched::on_timer_tick();

#ifdef BIGOS_TIMER_SMOKE
            // Validation-only bounded marker; intentionally kept out of on_tick().
            if (bigos::timer::ticks() <= TIMER_SMOKE_MARKER_LIMIT)
                serial_puts("BIGOS_TIMER_IRQ\n");
#endif
        }

        implement_isr(keyboard) {
            (void)__frame;
            const uint8_t scancode = inb(PS2_KEYBOARD_DATA_PORT);
            bigos::input::handle_keyboard_scancode(scancode);
        }

        void init_isr_timer() {
            driver::timer::pit::init_channel0();
            register_isr(VECTOR_TIMER, &isr_timer);
            driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);
        }

        void init_isr_keyboard() {
            register_isr(VECTOR_KEYBOARD, &isr_keyboard);
#ifdef BIGOS_KEYBOARD_SMOKE
            driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);
#endif
        }
    }   // namespace __detail

    void register_isr(uint64_t __vector, IRQHandler __isr) noexcept {
        irq::__detail::setISRHandler(__vector, __isr);
    }

    void init_isr() noexcept {
        __detail::init_isr_timer();
        __detail::init_isr_keyboard();
    }
}   // namespace irq::isr
NAMESPACE_BIGOS_END
