#ifndef _BIG_ARCH_VM_USER_BOUNDARY_H
#define _BIG_ARCH_VM_USER_BOUNDARY_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace irq {
    struct InterruptFrame;
}

namespace arch::vm_user {
    // Narrow core-facing boundary for the current x86_64 VM/user-entry path.
    // It is not a complete HAL and does not promise non-x86, UEFI, SMP, or wider
    // POSIX/user-memory behavior.
    struct UserResumeFrame {
        uint64_t opaque_words[22];
    };

    void init_user_entry() noexcept;
    void set_kernel_stack_top(uint64_t __stack_top) noexcept;

    uint64_t active_address_space_root() noexcept;
    bool is_active_address_space(uint64_t __root_phys) noexcept;
    void activate_address_space(uint64_t __root_phys) noexcept;
    void activate_kernel_address_space(uint64_t __root_phys) noexcept;
    void activate_user_address_space(uint64_t __root_phys) noexcept;

    bool is_user_return_frame(const irq::InterruptFrame *__frame) noexcept;
    void force_user_return_frame(irq::InterruptFrame *__frame, uint64_t __rflags) noexcept;
    bool capture_user_resume_frame(
        const irq::InterruptFrame *__source, uint64_t __return_value, UserResumeFrame *__out) noexcept;

    [[noreturn]] void enter_user(uint64_t __rip, uint64_t __rsp) noexcept;
    [[noreturn]] void resume_user(const UserResumeFrame *__frame) noexcept;
}   // namespace arch::vm_user
NAMESPACE_BIGOS_END

#endif   // _BIG_ARCH_VM_USER_BOUNDARY_H
