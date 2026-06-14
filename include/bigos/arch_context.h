#ifndef _BIG_ARCH_CONTEXT_H
#define _BIG_ARCH_CONTEXT_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    struct InterruptFrame;
}

namespace arch_context {
    // Narrow scheduler-facing boundary for the current x86_64 backend.
    //
    // This is not a generic HAL and does not promise SMP, UEFI, non-x86,
    // APIC/IOAPIC, or HPET parity. It exposes only the semantic context facts
    // consumed by IRQ-return preemption and the kernel context-switch boundary.
    bool is_kernel_irq_return_context(const irq::InterruptFrame *__frame) noexcept;
    bool is_user_irq_return_context(const irq::InterruptFrame *__frame) noexcept;

    // Enter the architecture-owned kernel context-switch primitive. The saved
    // stack pointer values are scheduler-owned opaque kernel context tokens; the
    // raw AMD64 callee-saved frame layout remains private to switch.s.
    void switch_kernel_context(uint64_t *__old_sp, uint64_t __new_sp) noexcept;
}   // namespace arch_context
NAMESPACE_BIGOS_END

#endif   // _BIG_ARCH_CONTEXT_H
