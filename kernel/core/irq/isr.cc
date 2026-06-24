#include <irq/isr.h>

#include <bigos/keyboard.h>
#include <bigos/device.h>
#include <bigos/io.h>
#include <bigos/percpu.h>
#include <bigos/sched.h>
#include <bigos/smp_ipi.h>
#include <bigos/timer.h>
#include <bigos/types.h>
#include <drivers/block/ata_pio.h>
#include <drivers/irqchip/i8259.h>
#include <drivers/irqchip/ioapic.h>
#include <drivers/irqchip/lapic.h>

NAMESPACE_BIGOS_BEG
namespace irq::isr {
    namespace __detail {
        constexpr uint16_t PS2_KEYBOARD_DATA_PORT = 0x60;
        constexpr bigos::timer::tick_t TIMER_SMOKE_MARKER_LIMIT = 3;
        bool g_ap_timer_marker_emitted[bigos::cpu::MAX_CPUS];
        bool g_lapic_timer_active = false;

        static void isr_timer(InterruptFrame *__frame);
        static void isr_keyboard(InterruptFrame *__frame);
        static void isr_primary_ide(InterruptFrame *__frame);

        bool select_default_irq_target(bigos::cpu::CpuId *__target_cpu, uint32_t *__target_apic_id) noexcept {
            const bigos::cpu::CpuSlot &bsp = bigos::cpu::slot_for(bigos::cpu::BOOTSTRAP_CPU_ID);
            if (bsp.startup_state != bigos::cpu::CpuStartupState::Online ||
                bsp.timer_state != bigos::cpu::LocalTimerState::Ready || bsp.apic_id == bigos::cpu::INVALID_APIC_ID)
                return false;

            if (__target_cpu != nullptr)
                *__target_cpu = bigos::cpu::BOOTSTRAP_CPU_ID;
            if (__target_apic_id != nullptr)
                *__target_apic_id = bsp.apic_id;
            return true;
        }

        void init_pic_timer_fallback() noexcept {
            g_lapic_timer_active = false;
            irq::__detail::setApicDefaultDeliveryActive(false);
            driver::irqchip::lapic::disable_timer();
            bigos::device::init_pit_timer();
            register_isr(VECTOR_TIMER, &isr_timer, VectorOwner::Pic);
            driver::irqchip::i8259::enable_irq(IRQ_LINE_TIMER);
            (void)bigos::cpu::set_local_timer_state(
                bigos::cpu::BOOTSTRAP_CPU_ID, bigos::cpu::LocalTimerState::Ready);
            serial_puts("BIGOS_APIC_DEFAULT_FALLBACK_PIC_PIT\n");
        }

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
            bigos::smp::handle_scheduler_nudge_ipi(__frame);
        }

        implement_isr(tlb_shootdown) {
            bigos::smp::handle_tlb_shootdown_ipi(__frame);
        }

        implement_isr(primary_ide) {
            (void)__frame;
            driver::block::ata_pio_primary_irq();
        }

        void init_isr_timer() {
#ifdef BIGOS_AP_STARTUP_PERCPU_TIMERS
            bigos::device::init_pit_timer();
            register_isr(VECTOR_LAPIC_TIMER, &isr_timer, VectorOwner::Lapic);
            uint32_t initial_count = 0;
            if (driver::irqchip::lapic::init() &&
                driver::irqchip::lapic::calibrate_timer_with_pit(&initial_count) &&
                (driver::irqchip::lapic::enable_x2apic() ||
                    driver::irqchip::lapic::status() == driver::irqchip::lapic::Status::Enabled) &&
                driver::irqchip::lapic::configure_timer(VECTOR_LAPIC_TIMER, initial_count, true)) {
                (void)bigos::cpu::set_local_timer_state(bigos::cpu::BOOTSTRAP_CPU_ID, bigos::cpu::LocalTimerState::Ready);
                g_lapic_timer_active = true;
                return;
            }

            (void)bigos::cpu::set_local_timer_state(bigos::cpu::BOOTSTRAP_CPU_ID, bigos::cpu::LocalTimerState::Failed);
#endif
            init_pic_timer_fallback();
        }

        bool init_ioapic_keyboard() noexcept {
#ifdef BIGOS_AP_STARTUP_PERCPU_TIMERS
            if (!g_lapic_timer_active || bigos::cpu::ioapic_count() == 0)
                return false;

            const bigos::cpu::IoApicDescriptor &ioapic = bigos::cpu::ioapic_for(0);
            bigos::cpu::CpuId target_cpu = bigos::cpu::BOOTSTRAP_CPU_ID;
            uint32_t target_apic_id = bigos::cpu::INVALID_APIC_ID;
            if (!ioapic.enabled || !select_default_irq_target(&target_cpu, &target_apic_id))
                return false;

            (void)target_cpu;
            if (!driver::irqchip::ioapic::init(ioapic.address))
                return false;

            const driver::irqchip::ioapic::RedirectionConfig config = {
                IRQ_LINE_KEYBOARD,
                VECTOR_KEYBOARD,
                target_apic_id,
                false,
                driver::irqchip::ioapic::TriggerMode::Edge,
                driver::irqchip::ioapic::Polarity::ActiveHigh,
            };
            if (!driver::irqchip::ioapic::route_irq(config))
                return false;

            driver::irqchip::i8259::disable_irq(IRQ_LINE_KEYBOARD);
            register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Lapic);
            irq::__detail::setApicDefaultDeliveryActive(true);
            serial_puts("BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE\n");
            return true;
#else
            return false;
#endif
        }

        void init_pic_keyboard_fallback() noexcept {
            register_isr(VECTOR_KEYBOARD, &isr_keyboard, VectorOwner::Pic);
            // Keyboard IRQ1 is enabled by default so the normal-boot interactive
            // /bin/sh can read commands from the TTY input ring. The keyboard_smoke
            // build also relied on this line; it now stays on unconditionally
            // because the default user-space init enters an interactive shell.
            driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);
        }

        void init_isr_keyboard() {
            if (!init_ioapic_keyboard()) {
                if (g_lapic_timer_active)
                    init_pic_timer_fallback();
                init_pic_keyboard_fallback();
            }
        }

        void init_isr_ide() noexcept {
            register_isr(VECTOR_PRIMARY_IDE, &isr_primary_ide, VectorOwner::Pic);
            driver::irqchip::i8259::enable_irq(IRQ_LINE_SLAVE);
            driver::irqchip::i8259::enable_irq(IRQ_LINE_PRIMARY_IDE);
        }
    }   // namespace __detail

    void register_isr(uint64_t __vector, IRQHandler __isr) noexcept {
        irq::__detail::setISRHandler(__vector, __isr);
    }

    void register_isr(uint64_t __vector, IRQHandler __isr, VectorOwner __owner) noexcept {
        irq::__detail::setISRHandler(__vector, __isr);
        irq::__detail::setVectorOwner(__vector, __owner);
    }

    void init_isr() noexcept {
        __detail::init_isr_timer();
        register_isr(VECTOR_SCHED_NUDGE, &__detail::isr_scheduler_nudge, VectorOwner::Lapic);
        register_isr(VECTOR_TLB_SHOOTDOWN, &__detail::isr_tlb_shootdown, VectorOwner::Lapic);
        __detail::init_isr_keyboard();
        __detail::init_isr_ide();
    }
}   // namespace irq::isr
NAMESPACE_BIGOS_END
