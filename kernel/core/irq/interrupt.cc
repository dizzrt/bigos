#include <bigos/io.h>
#include <bigos/panic.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#ifdef BIGOS_USER_PROCESS
#include <bigos/arch_vm_user_boundary.h>
#include <bigos/proc.h>
#include <bigos/signal.h>
#endif

#include <drivers/irqchip/i8259.h>
#include <irq/isr.h>
#include <irq/interrupt.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    namespace __detail {
        extern "C" void *isr_entries[IRQ_COUNT];

        constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
        constexpr uint16_t PRESENT_RING0_INTERRUPT_GATE = 0x8e00;
        constexpr uint16_t PRESENT_RING3_TRAP_GATE = 0xef00;

        INTRDescriptor kernel_idt[IRQ_COUNT];
        IRQHandler isr_list[IRQ_COUNT];

#ifdef BIGOS_USER_PROCESS
        static uint64_t *iret_tail(InterruptFrame *__frame) noexcept {
            return (uint64_t *)((uint8_t *)__frame + sizeof(InterruptFrame));
        }

        static void load_user_stack_tail(InterruptFrame *__frame) noexcept {
            if (__frame == nullptr || !bigos::arch::vm_user::is_user_return_frame(__frame))
                return;
            uint64_t *tail = iret_tail(__frame);
            __frame->rsp = tail[0];
            __frame->ss = tail[1];
        }

        static void store_user_stack_tail(InterruptFrame *__frame) noexcept {
            if (__frame == nullptr || !bigos::arch::vm_user::is_user_return_frame(__frame))
                return;
            uint64_t *tail = iret_tail(__frame);
            tail[0] = __frame->rsp;
            tail[1] = __frame->ss;
        }
#endif

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

        // Returns true when the fault was a recoverable user fault that has been
        // serviced and the faulting instruction should resume (the caller MUST
        // then skip the default exception handler). Returns false (or does not
        // return) for kernel faults and unrecoverable user faults.
        static bool page_fault_handler(InterruptFrame *__frame) noexcept {
            const uint64_t fault_address = read_cr2();
            const uint64_t error = __frame->error_code;
#ifdef BIGOS_USER_PROCESS
            const bool user_mode = bigos::arch::vm_user::is_user_return_frame(__frame);

            if (user_mode) {
                if (bigos::proc::try_handle_user_page_fault(fault_address, error))
                    return true;
                bigos::proc::fault_current_and_exit(-14);
            }
#endif

            serial_puts("BIGOS_PAGE_FAULT\n");
            kprintf("BIGOS_PAGE_FAULT address=%llx error=%llx rip=%llx\n", fault_address, error, __frame->rip);
            kprintf("BIGOS_PAGE_FAULT present=%d write=%d user=%d reserved-bit=%d instruction-fetch=%d\n",
                (uint32_t)(error & 0x1), (uint32_t)((error >> 1) & 0x1), (uint32_t)((error >> 2) & 0x1),
                (uint32_t)((error >> 3) & 0x1), (uint32_t)((error >> 4) & 0x1));
            halt_cpu();
            return false;
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

        class NonblockingContextGuard {
        public:
            NonblockingContextGuard() noexcept {
                bigos::sched::enter_nonblocking_context();
            }

            NonblockingContextGuard(const NonblockingContextGuard &) = delete;
            NonblockingContextGuard &operator=(const NonblockingContextGuard &) = delete;

            ~NonblockingContextGuard() noexcept {
                bigos::sched::leave_nonblocking_context();
            }
        };
    }   // namespace __detail

    void __detail::initIDT() noexcept {
        for (int i = 0; i < IRQ_COUNT; i++) {
            INTRDescriptor id(__detail::isr_entries[i]);
            id.selector = KERNEL_CODE_SELECTOR;
            id.attributes_brief = PRESENT_RING0_INTERRUPT_GATE;
            if (i == VECTOR_SYSCALL)
                id.attributes_brief = PRESENT_RING3_TRAP_GATE;
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
            __detail::NonblockingContextGuard nonblocking_guard;
            // CPU exceptions never send an i8259 EOI. A recoverable user page
            // fault is serviced in place; resume the faulting instruction without
            // falling through to the halting default exception handler.
            if (__frame->vector == VECTOR_PAGE_FAULT) {
                if (__detail::page_fault_handler(__frame))
                    return;
            }

            __detail::default_exception_handler(__frame);
            return;
        }

        if (__detail::is_i8259_external_irq(__frame->vector)) {
            const uint8_t irq_line = __detail::vector_to_i8259_irq(__frame->vector);
            {
                __detail::NonblockingContextGuard nonblocking_guard;
                if (driver::irqchip::i8259::is_spurious_irq(irq_line)) {
                    driver::irqchip::i8259::acknowledge_spurious_irq(irq_line);
                    return;
                }

                IRQHandler handler = __detail::isr_list[__frame->vector];
                if (handler == nullptr)
                    handler = &__detail::default_external_irq_handler;

                // External IRQs (including timer vector 0x20) send exactly one EOI
                // here, after the registered handler returns. Only after that EOI may
                // the scheduler-owned IRQ-return bridge switch to another runnable
                // kernel thread; exceptions and syscalls never enter this bridge.
                handler(__frame);
                driver::irqchip::i8259::send_eoi(irq_line);
            }
            bigos::sched::maybe_preempt_on_irq_return(__frame);
#ifdef BIGOS_USER_PROCESS
            // Single signal-delivery point (decision 2/9): only when returning to
            // a user-mode interrupted frame, after the scheduler IRQ-return bridge
            // has run and before iretq. Kernel-mode frames never deliver user
            // signals, and this path sends no i8259 EOI of its own. A default
            // Terminate disposition routes through fault_current_and_exit and does
            // not return here.
            if (bigos::arch::vm_user::is_user_return_frame(__frame)) {
                bigos::proc::Process *current = bigos::proc::current_process();
                if (current != nullptr && bigos::signal::has_deliverable_signal(current)) {
                    __detail::load_user_stack_tail(__frame);
                    bigos::signal::deliver_pending_to_user(__frame, current);
                    __detail::store_user_stack_tail(__frame);
                }
            }
#endif
            return;
        }

        if (__detail::is_syscall_vector(__frame->vector)) {
            // Syscall is a software interrupt, not an external IRQ: this path
            // MUST NOT send an i8259 EOI. It routes into the syscall dispatcher
            // and returns through the shared isr_common iretq path, leaving the
            // exception and external-IRQ branches (and their EOI semantics)
            // unchanged. The DPL=3 trap gate preserves IF so fd/VFS syscalls can
            // pass sched::can_block() in ordinary user process context.
            bigos::sys::dispatch(__frame);
            return;
        }

        __detail::NonblockingContextGuard nonblocking_guard;
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
