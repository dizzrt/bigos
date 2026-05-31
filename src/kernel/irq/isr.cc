#include <irq/isr.h>

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace irq::isr {
    IRQHandler isr_list[IRQ_COUNT];

    void register_isr(uint64_t __irq_num, IRQHandler __isr) noexcept {
        if (__irq_num >= IRQ_COUNT)
            return;

        isr_list[__irq_num] = __isr;
    }

    void init_isr() noexcept {
        // __detail::init_isr_keyboard();
    }
}   // namespace irq::isr
NAMESPACE_BIGOS_END
