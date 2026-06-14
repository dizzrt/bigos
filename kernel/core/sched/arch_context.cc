#include <bigos/arch_context.h>

#include <irq/interrupt.h>

NAMESPACE_BIGOS_BEG
namespace arch_context {
    namespace {
        constexpr uint64_t RPL_MASK = 0x3;
    }

    extern "C" void switch_context(uint64_t *__old_sp, uint64_t __new_sp) noexcept;

    bool is_kernel_irq_return_context(const irq::InterruptFrame *__frame) noexcept {
        return __frame != nullptr && (__frame->cs & RPL_MASK) == 0;
    }

    bool is_user_irq_return_context(const irq::InterruptFrame *__frame) noexcept {
        return __frame != nullptr && (__frame->cs & RPL_MASK) == RPL_MASK;
    }

    void switch_kernel_context(uint64_t *__old_sp, uint64_t __new_sp) noexcept {
        switch_context(__old_sp, __new_sp);
    }
}   // namespace arch_context
NAMESPACE_BIGOS_END
