#ifndef _BIG_INTERRUPT_H
#define _BIG_INTERRUPT_H

#include <bigos/types.h>

// idt = interrupt descriptor table
#define IDT_SIZE  0x1000
#define IDT_BASE  0x1000ul
#define IRQ_COUNT (IDT_SIZE / sizeof(bigos::irq::INTRDescriptor))

NAMESPACE_BIGOS_BEG
namespace irq {
    namespace __detail {
        struct INTRAttributes {
            uint16_t ist : 3;
            uint16_t reserved_0 : 5;
            uint16_t type : 4;
            uint16_t reserved_1 : 1;
            uint16_t dpl : 2;
            uint16_t p : 1;
        } __attribute__((packed));

        void initIDT() noexcept;
    }   // namespace __detail

    struct INTRDescriptor {
        uint16_t offset_low;
        uint16_t selector;
        union {
            uint16_t attributes_brief;
            __detail::INTRAttributes attributes;
        };
        uint16_t offset_mid;
        uint32_t offset_high;
        uint32_t reserved;

        void setISR(void *isr) noexcept {
            uint64_t isr_address = (uint64_t)isr;
            offset_low = isr_address;
            offset_mid = isr_address >> 16;
            offset_high = isr_address >> 32;
        }

        INTRDescriptor() noexcept
            : offset_low(0), selector(0), attributes_brief(0), offset_mid(0), offset_high(0), reserved(0) {}

        INTRDescriptor(void *isr) noexcept : INTRDescriptor() {
            this->setISR(isr);
        }
    };

    typedef void (*IRQHandler)(uint64_t irq_number, uint64_t error_code);

    inline void enableIRQ() noexcept {
        asm volatile("sti");
    }

    inline void disableIRQ() noexcept {
        asm volatile("cli");
    }

    void initIRQ() noexcept;
}   // namespace irq
NAMESPACE_BIGOS_END
#endif   // _BIG_INTERRUPT_H
