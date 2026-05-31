#include <irq/isr.h>

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace irq::isr {
    irq_handler isr_list[MAX_IRQ_NUM];

    void register_isr(uint64_t __irq_num, irq_handler __isr) {
        if (__irq_num > MAX_IRQ_NUM)
            return;

        isr_list[__irq_num] = __isr;
    }

    void init_isr() {
        // __detail::init_isr_keyboard();
    }
}   // namespace irq::isr
NAMESPACE_BIGOS_END
