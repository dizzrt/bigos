#include <drivers/irqchip/i8259.h>

#include <bigos/io.h>
#include <bigos/types.h>

NAMESPACE_DRIVER_BEG
namespace irqchip {
    namespace i8259 {
        void init() noexcept {
            // master
            bigos::outb(I8259_MASTER_ICW1, 0x11);
            bigos::outb(I8259_MASTER_ICW2, 0x20);
            bigos::outb(I8259_MASTER_ICW3, 0x04);
            bigos::outb(I8259_MASTER_ICW4, 0x01);
            bigos::outb(I8259_MASTER_OCW1, 0xff);

            // slave
            bigos::outb(I8259_SLAVE_ICW1, 0x11);
            bigos::outb(I8259_SLAVE_ICW2, 0x28);
            bigos::outb(I8259_SLAVE_ICW3, 0x02);
            bigos::outb(I8259_SLAVE_ICW4, 0x01);
            bigos::outb(I8259_SLAVE_OCW1, 0xff);
        }

        void enable_irq(uint8_t __irq) noexcept {
            uint8_t value;
            uint16_t port;

            if (__irq < 8) {
                port = I8259_MASTER_OCW1;
            } else {
                port = I8259_SLAVE_OCW1;
                __irq -= 8;
            }

            value = bigos::inb(port) & ~(1 << __irq);
            bigos::outb(port, value);
        }

        void disable_irq(uint8_t __irq) noexcept {
            uint8_t value;
            uint16_t port;

            if (__irq < 8) {
                port = I8259_MASTER_OCW1;
            } else {
                port = I8259_SLAVE_OCW1;
                __irq -= 8;
            }

            value = bigos::inb(port) | (1 << __irq);
            bigos::outb(port, value);
        }

        void send_eoi(uint16_t __irq) noexcept {
            if (__irq >= 8)
                bigos::outb(I8259_SLAVE_OCW2, I8259_EOI);

            bigos::outb(I8259_MASTER_OCW2, I8259_EOI);
        }
    }   // namespace i8259
}   // namespace irqchip
NAMESPACE_DRIVER_END
