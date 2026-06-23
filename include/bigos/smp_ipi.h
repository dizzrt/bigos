#ifndef _BIG_SMP_IPI_H
#define _BIG_SMP_IPI_H

#include <bigos/percpu.h>
#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    struct InterruptFrame;
}

namespace smp {
    enum class IpiType : uint8_t {
        SchedulerNudge,
        TlbShootdown,
    };

    enum class IpiDeliveryStatus : uint8_t {
        Delivered,
        LocalTarget,
        InvalidTarget,
        OfflineTarget,
        NonSchedulableTarget,
        MissingApicId,
        LapicUnavailable,
        SendFailed,
    };

    struct IpiDeliveryResult {
        IpiDeliveryStatus status;
        cpu::CpuId target_cpu;
        uint32_t target_apic_id;
        uint8_t vector;
    };

    IpiDeliveryResult send_ipi(cpu::CpuId __target_cpu, IpiType __type) noexcept;
    uint64_t send_ipi_mask(uint64_t __target_cpu_mask, IpiType __type, IpiDeliveryResult *__last_failure) noexcept;

    void handle_scheduler_nudge_ipi(irq::InterruptFrame *__frame) noexcept;
    void handle_tlb_shootdown_ipi(irq::InterruptFrame *__frame) noexcept;

    const IpiDeliveryResult &last_ipi_failure() noexcept;
}   // namespace smp
NAMESPACE_BIGOS_END

#endif   // _BIG_SMP_IPI_H
