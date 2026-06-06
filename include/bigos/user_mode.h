#ifndef _BIG_USER_MODE_H
#define _BIG_USER_MODE_H

#include <bigos/types.h>

namespace bigos::arch::x86 {
    constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
    constexpr uint16_t KERNEL_DATA_SELECTOR = 0x10;
    constexpr uint16_t KERNEL_STACK_SELECTOR = 0x18;
    constexpr uint16_t USER_DATA_SELECTOR = 0x23;
    constexpr uint16_t USER_CODE_SELECTOR = 0x2b;
    constexpr uint16_t TSS_SELECTOR = 0x30;

    void init_user_mode() noexcept;
    void set_tss_rsp0(uint64_t __rsp0) noexcept;
    [[noreturn]] void enter_user_mode(uint64_t __rip, uint64_t __rsp) noexcept;
}   // namespace bigos::arch::x86

#endif   // _BIG_USER_MODE_H
