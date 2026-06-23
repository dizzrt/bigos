#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/percpu.h>
#include <bigos/sched.h>
#include <bigos/smp_ipi.h>
#include <drivers/irqchip/lapic.h>
#include <irq/interrupt.h>

NAMESPACE_BIGOS_BEG
namespace smp {
    namespace {
        constexpr IpiDeliveryResult empty_result(IpiDeliveryStatus __status) noexcept {
            return {__status, cpu::MAX_CPUS, cpu::INVALID_APIC_ID, 0};
        }

        IpiDeliveryResult g_last_failure = empty_result(IpiDeliveryStatus::Delivered);

        uint8_t vector_for_type(IpiType __type) noexcept {
            switch (__type) {
                case IpiType::SchedulerNudge:
                    return irq::VECTOR_SCHED_NUDGE;
                case IpiType::TlbShootdown:
                    return irq::VECTOR_TLB_SHOOTDOWN;
            }
            return 0;
        }

        void record_failure(const IpiDeliveryResult &__result) noexcept {
            g_last_failure.status = __result.status;
            g_last_failure.target_cpu = __result.target_cpu;
            g_last_failure.target_apic_id = __result.target_apic_id;
            g_last_failure.vector = __result.vector;
        }
    }   // namespace

    IpiDeliveryResult send_ipi(cpu::CpuId __target_cpu, IpiType __type) noexcept {
        const uint8_t vector = vector_for_type(__type);
        IpiDeliveryResult result = {IpiDeliveryStatus::Delivered, __target_cpu, cpu::INVALID_APIC_ID, vector};

        if (__target_cpu == cpu::current_cpu_id()) {
            result.status = IpiDeliveryStatus::LocalTarget;
            return result;
        }
        if (!cpu::cpu_id_supported(__target_cpu)) {
            result.status = IpiDeliveryStatus::InvalidTarget;
            record_failure(result);
            return result;
        }
        if (!cpu::cpu_online(__target_cpu)) {
            result.status = IpiDeliveryStatus::OfflineTarget;
            record_failure(result);
            return result;
        }

        const cpu::CpuSlot &slot = cpu::slot_for(__target_cpu);
        result.target_apic_id = slot.apic_id;
        if (slot.timer_state != cpu::LocalTimerState::Ready) {
            result.status = IpiDeliveryStatus::NonSchedulableTarget;
            record_failure(result);
            return result;
        }
        if (slot.apic_id == cpu::INVALID_APIC_ID) {
            result.status = IpiDeliveryStatus::MissingApicId;
            record_failure(result);
            return result;
        }
        if (driver::irqchip::lapic::status() != driver::irqchip::lapic::Status::Enabled) {
            result.status = IpiDeliveryStatus::LapicUnavailable;
            record_failure(result);
            return result;
        }
        if (!driver::irqchip::lapic::send_fixed_ipi(slot.apic_id, vector)) {
            result.status = IpiDeliveryStatus::SendFailed;
            record_failure(result);
            return result;
        }
#ifdef BIGOS_TLB_SHOOTDOWN_SMOKE
        if (__type == IpiType::TlbShootdown)
            serial_puts("BIGOS_TLB_SHOOTDOWN_IPI_DELIVERED\n");
#endif
        return result;
    }

    uint64_t send_ipi_mask(uint64_t __target_cpu_mask, IpiType __type, IpiDeliveryResult *__last_failure) noexcept {
        uint64_t delivered_mask = 0;
        for (cpu::CpuId id = 0; id < cpu::MAX_CPUS; id++) {
            const uint64_t bit = 1ull << id;
            if ((__target_cpu_mask & bit) == 0)
                continue;
            const IpiDeliveryResult result = send_ipi(id, __type);
            if (result.status == IpiDeliveryStatus::Delivered) {
                delivered_mask |= bit;
            } else if (__last_failure != nullptr) {
                *__last_failure = result;
            }
        }
        return delivered_mask;
    }

    void handle_scheduler_nudge_ipi(irq::InterruptFrame *__frame) noexcept {
        (void)__frame;
        sched::on_scheduler_nudge();
    }

    void handle_tlb_shootdown_ipi(irq::InterruptFrame *__frame) noexcept {
        (void)__frame;
#ifdef BIGOS_TLB_SHOOTDOWN_SMOKE
        serial_puts("BIGOS_TLB_SHOOTDOWN_IPI\n");
#endif
        mm::handle_tlb_shootdown_ipi();
    }

    const IpiDeliveryResult &last_ipi_failure() noexcept {
        return g_last_failure;
    }
}   // namespace smp
NAMESPACE_BIGOS_END
