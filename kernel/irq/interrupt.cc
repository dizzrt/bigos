#include <bigos/io.h>

#include <irq/isr.h>
#include <irq/interrupt.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    namespace __detail {
        extern "C" void *isr_entries[IRQ_COUNT];

        extern "C" IRQHandler isr_list[IRQ_COUNT];
        IRQHandler isr_list[IRQ_COUNT];

        static void default_isr(uint64_t irq_num, uint64_t ecode) {
            kprintf("default ISR called with irq number: %lld and error code: %lld\n", irq_num, ecode);
        }
    }   // namespace __detail

    void __detail::initIDT() {
        INTRDescriptor *idt = (INTRDescriptor *)IDT_BASE;

        for (int i = 0; i < IRQ_COUNT; i++) {
            INTRDescriptor id(__detail::isr_entries[i]);
            id.selector = 0x08;
            id.attributes_brief = 0x8e00;
            idt[i] = id;

            isr_list[i] = &__detail::default_isr;
        }
    }

    void initIRQ() {
        __detail::initIDT();
    }
}   // namespace irq
NAMESPACE_BIGOS_END