#ifndef _BIG_ISR_H
#define _BIG_ISR_H

#include <irq/interrupt.h>
#include <bigos/types.h>

#define extern_isr(ISR_NAME)    void init_isr_##ISR_NAME()
#define implement_isr(ISR_NAME) static void isr_##ISR_NAME(InterruptFrame *__frame)

NAMESPACE_BIGOS_BEG
namespace irq::isr {

    namespace __detail {
        // isrs are implemented in file project_dir/irq/isr/isr_xxx.cc
        extern_isr(timer);
        extern_isr(keyboard);
    }   // namespace __detail

    void register_isr(uint64_t __vector, IRQHandler __isr) noexcept;
    void register_isr(uint64_t __vector, IRQHandler __isr, VectorOwner __owner) noexcept;
    void init_isr() noexcept;
}   // namespace irq::isr
NAMESPACE_BIGOS_END
#endif   // _BIG_ISR_H
