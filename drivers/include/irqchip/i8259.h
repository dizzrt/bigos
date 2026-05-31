#ifndef _BIG_I8259_H
#define _BIG_I8259_H

#include <bigos/types.h>

#define I8259_MASTER_ICW1 0x20
#define I8259_MASTER_ICW2 0x21
#define I8259_MASTER_ICW3 0x21
#define I8259_MASTER_ICW4 0x21
#define I8259_MASTER_OCW1 0x21
#define I8259_MASTER_OCW2 0x20
#define I8259_MASTER_OCW3 0x20

#define I8259_SLAVE_ICW1 0xa0
#define I8259_SLAVE_ICW2 0xa1
#define I8259_SLAVE_ICW3 0xa1
#define I8259_SLAVE_ICW4 0xa1
#define I8259_SLAVE_OCW1 0xa1
#define I8259_SLAVE_OCW2 0xa0
#define I8259_SLAVE_OCW3 0xa0

#define I8259_IRQ_TIMER         0
#define I8259_IRQ_KEYBOARD      1
#define I8259_IRQ_SLAVE         2
#define I8259_IRQ_COMB          3
#define I8259_IRQ_COMA          4
#define I8259_IRQ_LPTB          5
#define I8259_IRQ_FLOPPY        6
#define I8259_IRQ_LPTA          7
#define I8259_IRQ_RTC           8
#define I8259_IRQ_PS2_MOUSE     12
#define I8259_IRQ_FPU           13
#define I8259_IRQ_PRIMARY_IDE   14
#define I8259_IRQ_SECONDARY_IDE 15

#define I8259_EOI      0x20
#define I8259_IRQ_BASE 0x20

#define i8259_get_irq_num(irq) (irq + I8259_IRQ_BASE)

NAMESPACE_DRIVER_BEG
namespace irqchip {
    namespace i8259 {
        void init() noexcept;

        void enable_irq(uint8_t __irq) noexcept;
        void disable_irq(uint8_t __irq) noexcept;

        void send_eoi(uint16_t __irq) noexcept;
    }   // namespace i8259
}   // namespace irqchip
NAMESPACE_DRIVER_END
#endif   // _BIG_I8259_H
