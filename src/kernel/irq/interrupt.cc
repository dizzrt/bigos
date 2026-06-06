#include <bigos/io.h>
#include <bigos/panic.h>
#include <bigos/syscall.h>

#include <drivers/irqchip/i8259.h>
#include <irq/isr.h>
#include <irq/interrupt.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    namespace __detail {
        extern "C" void *isr_entries[IRQ_COUNT];

        constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
        constexpr uint16_t PRESENT_RING0_INTERRUPT_GATE = 0x8e00;

        INTRDescriptor kernel_idt[IRQ_COUNT];
        IRQHandler isr_list[IRQ_COUNT];

        [[noreturn]] static void halt_cpu() noexcept {
            bigos::khalt();
        }

        static uint64_t read_cr2() noexcept {
            uint64_t cr2;
            asm volatile("movq %%cr2, %0" : "=r"(cr2));
            return cr2;
        }

        static void default_exception_handler(InterruptFrame *__frame) noexcept {
            kprintf("BIGOS_EXCEPTION vector=%x error=%llx rip=%llx cs=%llx rflags=%llx rsp=%llx\n",
                (uint32_t)__frame->vector, __frame->error_code, __frame->rip, __frame->cs, __frame->rflags,
                __frame->rsp);
            halt_cpu();
        }

        static void page_fault_handler(InterruptFrame *__frame) noexcept {
            const uint64_t fault_address = read_cr2();
            const uint64_t error = __frame->error_code;

            serial_puts("BIGOS_PAGE_FAULT\n");
            kprintf("BIGOS_PAGE_FAULT address=%llx error=%llx rip=%llx\n", fault_address, error, __frame->rip);
            kprintf("BIGOS_PAGE_FAULT present=%d write=%d user=%d reserved-bit=%d instruction-fetch=%d\n",
                (uint32_t)(error & 0x1), (uint32_t)((error >> 1) & 0x1), (uint32_t)((error >> 2) & 0x1),
                (uint32_t)((error >> 3) & 0x1), (uint32_t)((error >> 4) & 0x1));
            halt_cpu();
        }

        static void default_external_irq_handler(InterruptFrame *__frame) noexcept {
            const uint8_t irq_line = (uint8_t)(__frame->vector - I8259_MASTER_VECTOR_BASE);
            kprintf("BIGOS_UNHANDLED_IRQ vector=%x irq=%x\n", (uint32_t)__frame->vector, (uint32_t)irq_line);
        }

        static void unknown_vector_handler(InterruptFrame *__frame) noexcept {
            kprintf("BIGOS_UNKNOWN_VECTOR vector=%x error=%llx rip=%llx\n", (uint32_t)__frame->vector,
                __frame->error_code, __frame->rip);
        }

        static bool is_cpu_exception(uint64_t __vector) noexcept {
            return __vector <= CPU_EXCEPTION_VECTOR_LAST;
        }

        static bool is_i8259_external_irq(uint64_t __vector) noexcept {
            return __vector >= I8259_MASTER_VECTOR_BASE && __vector <= I8259_VECTOR_LAST;
        }

        static bool is_syscall_vector(uint64_t __vector) noexcept {
            return __vector == VECTOR_SYSCALL;
        }

        static uint8_t vector_to_i8259_irq(uint64_t __vector) noexcept {
            return (uint8_t)(__vector - I8259_MASTER_VECTOR_BASE);
        }

        static void load_idt(const IDTPointer *__ptr) noexcept {
            asm volatile("lidt (%0)" : : "r"(__ptr) : "memory");
        }
    }   // namespace __detail

    void __detail::initIDT() noexcept {
        for (int i = 0; i < IRQ_COUNT; i++) {
            INTRDescriptor id(__detail::isr_entries[i]);
            id.selector = KERNEL_CODE_SELECTOR;
            id.attributes_brief = PRESENT_RING0_INTERRUPT_GATE;
            id.reserved = 0;
            kernel_idt[i] = id;

            isr_list[i] = nullptr;
        }

        const IDTPointer idt_ptr = {(uint16_t)(sizeof(kernel_idt) - 1), (uint64_t)&kernel_idt[0]};
        load_idt(&idt_ptr);
    }

    void __detail::setISRHandler(uint64_t __vector, IRQHandler __handler) noexcept {
        if (__vector >= IRQ_COUNT)
            return;

        isr_list[__vector] = __handler;
    }

    extern "C" void irq_dispatch(InterruptFrame *__frame) {
        if (__frame == nullptr)
            return;

        if (__detail::is_cpu_exception(__frame->vector)) {
            // CPU exceptions never send an i8259 EOI.
            if (__frame->vector == VECTOR_PAGE_FAULT)
                __detail::page_fault_handler(__frame);

            __detail::default_exception_handler(__frame);
            return;
        }

        if (__detail::is_i8259_external_irq(__frame->vector)) {
            const uint8_t irq_line = __detail::vector_to_i8259_irq(__frame->vector);
            if (driver::irqchip::i8259::is_spurious_irq(irq_line)) {
                driver::irqchip::i8259::acknowledge_spurious_irq(irq_line);
                return;
            }

            IRQHandler handler = __detail::isr_list[__frame->vector];
            if (handler == nullptr)
                handler = &__detail::default_external_irq_handler;

            // External IRQs (including timer vector 0x20) send exactly one EOI
            // here, after the registered handler returns, then iretq via isr_common.
            handler(__frame);
            driver::irqchip::i8259::send_eoi(irq_line);
            return;
        }

        if (__detail::is_syscall_vector(__frame->vector)) {
            // Syscall is a software interrupt, not an external IRQ: this path
            // MUST NOT send an i8259 EOI. It routes into the syscall dispatcher
            // and returns through the shared isr_common iretq path, leaving the
            // exception and external-IRQ branches (and their EOI semantics)
            // unchanged.
            bigos::sys::dispatch(__frame);
            return;
        }

        __detail::unknown_vector_handler(__frame);
    }

    void initIRQ() noexcept {
        __detail::initIDT();
        driver::irqchip::i8259::init();
        isr::init_isr();
    }

    void triggerPageFaultForValidation() noexcept {
        volatile uint64_t *fault_address = (volatile uint64_t *)0x0000400000000000ull;
        *fault_address = 0;
    }
}   // namespace irq
NAMESPACE_BIGOS_END
