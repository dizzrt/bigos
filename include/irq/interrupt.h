#ifndef _BIG_INTERRUPT_H
#define _BIG_INTERRUPT_H

#include <bigos/types.h>

// idt = interrupt descriptor table
#define IDT_SIZE  0x1000
#define IDT_BASE  0x1000ul
#define IRQ_COUNT (IDT_SIZE / sizeof(bigos::irq::INTRDescriptor))

NAMESPACE_BIGOS_BEG
namespace irq {
    constexpr uint8_t CPU_EXCEPTION_VECTOR_FIRST = 0x00;
    constexpr uint8_t CPU_EXCEPTION_VECTOR_LAST = 0x1f;
    constexpr uint8_t I8259_MASTER_VECTOR_BASE = 0x20;
    constexpr uint8_t I8259_SLAVE_VECTOR_BASE = 0x28;
    constexpr uint8_t I8259_VECTOR_COUNT = 16;
    constexpr uint8_t I8259_VECTOR_LAST = I8259_MASTER_VECTOR_BASE + I8259_VECTOR_COUNT - 1;

    constexpr uint8_t IRQ_LINE_TIMER = 0;
    constexpr uint8_t IRQ_LINE_KEYBOARD = 1;
    constexpr uint8_t IRQ_LINE_SLAVE = 2;
    constexpr uint8_t IRQ_LINE_RTC = 8;
    constexpr uint8_t IRQ_LINE_PS2_MOUSE = 12;
    constexpr uint8_t IRQ_LINE_PRIMARY_IDE = 14;
    constexpr uint8_t IRQ_LINE_SECONDARY_IDE = 15;

    constexpr uint8_t VECTOR_PAGE_FAULT = 0x0e;
    constexpr uint8_t VECTOR_TIMER = I8259_MASTER_VECTOR_BASE + IRQ_LINE_TIMER;
    constexpr uint8_t VECTOR_KEYBOARD = I8259_MASTER_VECTOR_BASE + IRQ_LINE_KEYBOARD;

    struct InterruptFrame;
    typedef void (*IRQHandler)(InterruptFrame *__frame);

    namespace __detail {
        struct INTRAttributes {
            uint16_t ist : 3;
            uint16_t reserved_0 : 5;
            uint16_t type : 4;
            uint16_t reserved_1 : 1;
            uint16_t dpl : 2;
            uint16_t p : 1;
        } __attribute__((packed));

        struct IDTPointer {
            uint16_t limit;
            uint64_t base;
        } __attribute__((packed));

        void initIDT() noexcept;
        void setISRHandler(uint64_t __vector, IRQHandler __handler) noexcept;
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

    struct InterruptFrame {
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rbp;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;
        uint64_t rsp;
        uint64_t ss;
        uint64_t vector;
        uint64_t error_code;
        uint64_t rip;
        uint64_t cs;
        uint64_t rflags;
    } __attribute__((packed));

    inline void enableIRQ() noexcept {
        // Only early registered IRQ smoke handlers are enabled; kernel APIs are not IRQ-context safe yet.
        asm volatile("sti");
    }

    inline void disableIRQ() noexcept {
        asm volatile("cli");
    }

    class InterruptGuard {
    private:
        static constexpr uint64_t RFLAGS_IF = 1ull << 9;
        bool restore_enabled_;

        static uint64_t read_rflags() noexcept {
            uint64_t flags;
            asm volatile("pushfq; popq %0" : "=r"(flags)::"memory");
            return flags;
        }

    public:
        // Single-core guard for same-CPU maskable IRQ interleaving only.
        // It is not an SMP lock, NMI guard, blocking primitive, or scheduler lock.
        InterruptGuard() noexcept : restore_enabled_((read_rflags() & RFLAGS_IF) != 0) {
            asm volatile("cli" ::: "memory");
        }

        InterruptGuard(const InterruptGuard &) = delete;
        InterruptGuard &operator=(const InterruptGuard &) = delete;

        ~InterruptGuard() noexcept {
            if (restore_enabled_)
                asm volatile("sti" ::: "memory");
        }
    };

    void initIRQ() noexcept;
    void triggerPageFaultForValidation() noexcept;
}   // namespace irq
NAMESPACE_BIGOS_END
#endif   // _BIG_INTERRUPT_H
