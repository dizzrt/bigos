#ifndef _BIG_USER_MODE_H
#define _BIG_USER_MODE_H

#include <bigos/percpu.h>
#include <bigos/types.h>

namespace bigos::irq {
    struct InterruptFrame;
}   // namespace bigos::irq

namespace bigos::arch::x86 {
    constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
    constexpr uint16_t KERNEL_DATA_SELECTOR = 0x10;
    constexpr uint16_t KERNEL_STACK_SELECTOR = 0x18;
    constexpr uint16_t USER_DATA_SELECTOR = 0x23;
    constexpr uint16_t USER_CODE_SELECTOR = 0x2b;
    constexpr uint16_t TSS_SELECTOR = 0x30;

    void init_user_mode() noexcept;
    void init_cpu_mode(bigos::cpu::CpuId __cpu_id) noexcept;
    void set_tss_rsp0(uint64_t __rsp0) noexcept;
    void set_cpu_tss_rsp0(bigos::cpu::CpuId __cpu_id, uint64_t __rsp0) noexcept;
    [[noreturn]] void enter_user_mode(uint64_t __rip, uint64_t __rsp) noexcept;
    // Resumes ring3 from a fully saved InterruptFrame (used by a forked child's
    // first entry). The frame register state is restored verbatim, so the child
    // resumes exactly where the parent's int 0x80 returned, with rax already set
    // to the child's fork return value (0).
    [[noreturn]] void enter_user_mode_frame(const bigos::irq::InterruptFrame *__frame) noexcept;
}   // namespace bigos::arch::x86

#endif   // _BIG_USER_MODE_H
