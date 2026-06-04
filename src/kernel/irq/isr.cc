#include <irq/isr.h>

#include <bigos/io.h>
#include <bigos/types.h>
#include <drivers/irqchip/i8259.h>

NAMESPACE_BIGOS_BEG
namespace irq::isr {
    namespace __detail {
        constexpr uint16_t PS2_KEYBOARD_DATA_PORT = 0x60;

        implement_isr(keyboard) {
            (void)__frame;
            const uint8_t scancode = inb(PS2_KEYBOARD_DATA_PORT);
            serial_puts("BIGOS_KEYBOARD_IRQ\n");
            kprintf("BIGOS_KEYBOARD_IRQ scancode=%x\n", (uint32_t)scancode);
        }

        void init_isr_keyboard() {
            register_isr(VECTOR_KEYBOARD, &isr_keyboard);
            driver::irqchip::i8259::enable_irq(IRQ_LINE_KEYBOARD);
        }
    }   // namespace __detail

    void register_isr(uint64_t __vector, IRQHandler __isr) noexcept {
        irq::__detail::setISRHandler(__vector, __isr);
    }

    void init_isr() noexcept {
        __detail::init_isr_keyboard();
    }
}   // namespace irq::isr
NAMESPACE_BIGOS_END
