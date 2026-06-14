#include <bigos/arch_vm_user_boundary.h>

#include <bigos/memory.h>
#include <bigos/user_mode.h>
#include <irq/interrupt.h>
#include <string.h>

NAMESPACE_BIGOS_BEG
namespace arch::vm_user {
    namespace {
        constexpr uint64_t ADDRESS_SPACE_ROOT_MASK = 0x000ffffffffff000ull;
        constexpr uint64_t RPL_MASK = 0x3;
        constexpr uint64_t USER_RPL = 0x3;

        const irq::InterruptFrame *as_interrupt_frame(const UserResumeFrame *__frame) noexcept {
            return (const irq::InterruptFrame *)__frame->opaque_words;
        }
    }   // namespace

    static_assert(sizeof(UserResumeFrame) == sizeof(irq::InterruptFrame));

    void init_user_entry() noexcept {
        bigos::arch::x86::init_user_mode();
    }

    void set_kernel_stack_top(uint64_t __stack_top) noexcept {
        bigos::arch::x86::set_tss_rsp0(__stack_top);
    }

    uint64_t active_address_space_root() noexcept {
        return bigos::mm::read_cr3() & ADDRESS_SPACE_ROOT_MASK;
    }

    bool is_active_address_space(uint64_t __root_phys) noexcept {
        return active_address_space_root() == (__root_phys & ADDRESS_SPACE_ROOT_MASK);
    }

    void activate_address_space(uint64_t __root_phys) noexcept {
        bigos::mm::activate_address_space_root(__root_phys);
    }

    void activate_kernel_address_space(uint64_t __root_phys) noexcept {
        activate_address_space(__root_phys);
    }

    void activate_user_address_space(uint64_t __root_phys) noexcept {
        activate_address_space(__root_phys);
    }

    bool is_user_return_frame(const irq::InterruptFrame *__frame) noexcept {
        return __frame != nullptr && (__frame->cs & RPL_MASK) == USER_RPL;
    }

    void force_user_return_frame(irq::InterruptFrame *__frame, uint64_t __rflags) noexcept {
        if (__frame == nullptr)
            return;
        __frame->cs = bigos::arch::x86::USER_CODE_SELECTOR;
        __frame->ss = bigos::arch::x86::USER_DATA_SELECTOR;
        __frame->rflags = __rflags;
    }

    bool capture_user_resume_frame(
        const irq::InterruptFrame *__source, uint64_t __return_value, UserResumeFrame *__out) noexcept {
        if (__source == nullptr || __out == nullptr || !is_user_return_frame(__source))
            return false;

        irq::InterruptFrame frame = *__source;
        frame.rax = __return_value;

        const uint64_t *iret_tail = (const uint64_t *)((const uint8_t *)__source + sizeof(irq::InterruptFrame));
        frame.rsp = iret_tail[0];
        frame.ss = iret_tail[1];

        memcpy(__out, &frame, sizeof(frame));
        return true;
    }

    [[noreturn]] void enter_user(uint64_t __rip, uint64_t __rsp) noexcept {
        bigos::arch::x86::enter_user_mode(__rip, __rsp);
    }

    [[noreturn]] void resume_user(const UserResumeFrame *__frame) noexcept {
        bigos::arch::x86::enter_user_mode_frame(as_interrupt_frame(__frame));
    }
}   // namespace arch::vm_user
NAMESPACE_BIGOS_END
