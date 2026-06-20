#include <irq/isr.h>

#include <bigos/keyboard.h>
#include <bigos/device.h>
#include <bigos/io.h>
#include <bigos/percpu.h>
#include <bigos/sched.h>
#include <bigos/timer.h>
#include <bigos/types.h>
#include <drivers/irqchip/i8259.h>
#include <drivers/irqchip/lapic.h>

NAMESPACE_BIGOS_BEG
namespace irq::isr {
    namespace __detail {
        constexpr uint16_t PS2_KEYBOARD_DATA_PORT = 0x60;
        constexpr bigos::timer::tick_t TIMER_SMOKE_MARKER_LIMIT = 3;
        bool g_ap_timer_marker_emitted[bigos::cpu::MAX_CPUS];

        implement_isr(timer) {
            (void)__frame;
            const bigos::cpu::CpuId cpu_id = bigos::cpu::current_cpu_id();
            (void)bigos::cpu::record_local_timer_tick(cpu_id);
            if (cpu_id != bigos::cpu::BOOTSTRAP_CPU_ID) {
                if (cpu_id < bigos::cpu::MAX_CPUS && !g_ap_timer_marker_emitted[cpu_id]) {
                    g_ap_timer_marker_emitted[cpu_id] = true;
                    serial_puts("BIGOS_AP_LOCAL_TIMER\n");
                }
                bigos::sched::on_timer_tick();
                return;
            }

            // Advance the monotonic tick through the timer-owned IRQ-context-safe
            // API. The IRQ layer does not mutate timer-internal tick state directly,
            // and does not send i8259 EOI here; EOI stays owned by irq_dispatch.
            bigos::timer::on_tick();

            // Bounded IRQ-context-safe hook: accounts the current time slice,
            // records reschedule intent, and wakes deadline waiters. It never
            // allocates, blocks, sends EOI, or switches directly from this path.
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

        implement_isr(scheduler_nudge) {
            (void)__frame;
            bigos::sched::on_scheduler_nudge();
        }

        void init_isr_timer() {
#ifdef BIGOS_AP_STARTUP_PERCPU_TIMERS
            bigos::device::init_pit_timer();
            register_isr(VECTOR_LAPIC_TIMER, &isr_timer);
            uint32_t initial_count = 0;
            if (driver::irqchip::lapic::init() &&
                driver::irqchip::lapic::calibrate_timer_with_pit(&initial_count) &&
                (driver::irqchip::lapic::enable_x2apic() ||
                    driver::irqchip::lapic::status() == driver::irqchip::lapic::Status::Enabled) &&
                driver::irqchip::lapic::configure_timer(VECTOR_LAPIC_TIMER, initial_count, true)) {
                (void)bigos::cpu::set_local_timer_state(bigos::cpu::BOOTSTRAP_CPU_ID, bigos::cpu::LocalTimerState::Ready);
                return;
            }

            driver::irqchip::lapic::disable_timer();
            (void)bigos::cpu::set_local_timer_state(bigos::cpu::BOOTSTRAP_CPU_ID, bigos::cpu::LocalTimerState::Failed);
#endif
            bigos::device::init_pit_timer();
            register_isr(VECTOR_TIMER, &isr_timer);
            driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);
            (void)bigos::cpu::set_local_timer_state(bigos::cpu::BOOTSTRAP_CPU_ID, bigos::cpu::LocalTimerState::Ready);
        }

        void init_isr_keyboard() {
            register_isr(VECTOR_KEYBOARD, &isr_keyboard);
            // Keyboard IRQ1 is enabled by default so the normal-boot interactive
            // /bin/sh can read commands from the TTY input ring. The keyboard_smoke
            // build also relied on this line; it now stays on unconditionally
            // because the default user-space init enters an interactive shell.
            driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);
        }
    }   // namespace __detail

    void register_isr(uint64_t __vector, IRQHandler __isr) noexcept {
        irq::__detail::setISRHandler(__vector, __isr);
    }

    void init_isr() noexcept {
        __detail::init_isr_timer();
        register_isr(VECTOR_SCHED_NUDGE, &__detail::isr_scheduler_nudge);
        __detail::init_isr_keyboard();
    }
}   // namespace irq::isr
NAMESPACE_BIGOS_END
