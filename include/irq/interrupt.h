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
    constexpr uint8_t VECTOR_LAPIC_TIMER = 0xef;
    constexpr uint8_t VECTOR_SCHED_NUDGE = 0xee;
    constexpr uint8_t VECTOR_TLB_SHOOTDOWN = 0xed;

    // Software-interrupt syscall entry vector. The default-off first user program
    // raises only this IDT gate to DPL=3 so CPL3 can trigger `int 0x80`; exception
    // and external IRQ gates remain ring0-only.
    constexpr uint8_t VECTOR_SYSCALL = 0x80;

    struct InterruptFrame;
    typedef void (*IRQHandler)(InterruptFrame *__frame);

    enum class VectorOwner : uint8_t {
        Unknown,
        CpuException,
        Syscall,
        Pic,
        Lapic,
        ApicSpurious,
    };

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
        void setVectorOwner(uint64_t __vector, VectorOwner __owner) noexcept;
        void setApicDefaultDeliveryActive(bool __active) noexcept;
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

    static_assert(sizeof(InterruptFrame) == 176);
    static_assert(__builtin_offsetof(InterruptFrame, r15) == 0);
    static_assert(__builtin_offsetof(InterruptFrame, rax) == 112);
    static_assert(__builtin_offsetof(InterruptFrame, rsp) == 120);
    static_assert(__builtin_offsetof(InterruptFrame, ss) == 128);
    static_assert(__builtin_offsetof(InterruptFrame, vector) == 136);
    static_assert(__builtin_offsetof(InterruptFrame, error_code) == 144);
    static_assert(__builtin_offsetof(InterruptFrame, rip) == 152);
    static_assert(__builtin_offsetof(InterruptFrame, cs) == 160);
    static_assert(__builtin_offsetof(InterruptFrame, rflags) == 168);

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
    bool apic_default_delivery_active() noexcept;
    const char *vector_owner_name(VectorOwner __owner) noexcept;
    void triggerPageFaultForValidation() noexcept;
}   // namespace irq
NAMESPACE_BIGOS_END
#endif   // _BIG_INTERRUPT_H
