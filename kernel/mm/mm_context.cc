#include <bigos/memory.h>
#include <bigos/percpu.h>
#include <irq/interrupt.h>

NAMESPACE_BIGOS_BEG
namespace mm {
    namespace {
        constexpr uint64_t ADDRESS_SPACE_ROOT_MASK = 0x000ffffffffff000ull;

        void spin_lock(volatile uint32_t *__lock) noexcept {
            while (__atomic_exchange_n(__lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
                while (__atomic_load_n(__lock, __ATOMIC_RELAXED) != 0u)
                    asm volatile("pause" ::: "memory");
            }
        }

        void spin_unlock(volatile uint32_t *__lock) noexcept {
            __atomic_store_n(__lock, 0u, __ATOMIC_RELEASE);
        }

        uint64_t cpu_bit(cpu::CpuId __id) noexcept {
            return __id < cpu::MAX_CPUS ? (1ull << __id) : 0;
        }
    }   // namespace

    MmContext *create_mm_context(uint64_t __root_phys) noexcept {
        if (__root_phys == INVALID_PHYS_ADDR)
            return nullptr;
        void *raw = bigos::kmalloc(sizeof(MmContext));
        if (raw == nullptr)
            return nullptr;
        auto *context = (MmContext *)raw;
        context->root_phys = __root_phys & ADDRESS_SPACE_ROOT_MASK;
        context->lock = 0;
        context->refcount = 1;
        context->active_cpu_mask = 0;
        context->shootdown_generation = 0;
        context->dying = false;
        return context;
    }

    void retain_mm_context(MmContext *__context) noexcept {
        if (__context != nullptr)
            __atomic_fetch_add(&__context->refcount, 1u, __ATOMIC_ACQ_REL);
    }

    void release_mm_context(MmContext *__context) noexcept {
        if (__context == nullptr)
            return;
        const uint32_t previous = __atomic_fetch_sub(&__context->refcount, 1u, __ATOMIC_ACQ_REL);
        if (previous == 1)
            bigos::free(__context);
    }

    void mark_mm_context_dying(MmContext *__context) noexcept {
        if (__context == nullptr)
            return;
        irq::InterruptGuard guard;
        spin_lock(&__context->lock);
        __context->dying = true;
        __atomic_fetch_add(&__context->shootdown_generation, 1ull, __ATOMIC_ACQ_REL);
        spin_unlock(&__context->lock);
    }

    uint64_t mm_context_root(const MmContext *__context) noexcept {
        return __context != nullptr ? (__context->root_phys & ADDRESS_SPACE_ROOT_MASK) : INVALID_PHYS_ADDR;
    }

    uint64_t mm_context_active_cpu_mask(const MmContext *__context) noexcept {
        if (__context == nullptr)
            return 0;
        return __atomic_load_n(&__context->active_cpu_mask, __ATOMIC_ACQUIRE);
    }

    uint64_t mm_context_shootdown_generation(const MmContext *__context) noexcept {
        if (__context == nullptr)
            return 0;
        return __atomic_load_n(&__context->shootdown_generation, __ATOMIC_ACQUIRE);
    }

    bool enter_mm_context(MmContext *__context) noexcept {
        if (__context == nullptr)
            return false;

        auto *old = (MmContext *)cpu::current_state().current_mm_context;
        if (old == __context)
            return true;

        const cpu::CpuId id = cpu::current_cpu_id();
        const uint64_t bit = cpu_bit(id);
        if (bit == 0)
            return false;

        irq::InterruptGuard guard;
        spin_lock(&__context->lock);
        if (__context->dying) {
            spin_unlock(&__context->lock);
            return false;
        }
        retain_mm_context(__context);
        __atomic_fetch_or(&__context->active_cpu_mask, bit, __ATOMIC_ACQ_REL);
        spin_unlock(&__context->lock);

        cpu::set_current_mm_context(__context);

        if (old != nullptr) {
            __atomic_fetch_and(&old->active_cpu_mask, ~bit, __ATOMIC_ACQ_REL);
            release_mm_context(old);
        }
        return true;
    }

    void leave_current_mm_context() noexcept {
        auto *context = (MmContext *)cpu::current_state().current_mm_context;
        if (context == nullptr)
            return;
        const uint64_t bit = cpu_bit(cpu::current_cpu_id());
        cpu::set_current_mm_context(nullptr);
        if (bit != 0)
            __atomic_fetch_and(&context->active_cpu_mask, ~bit, __ATOMIC_ACQ_REL);
        release_mm_context(context);
    }

    uint64_t mm_context_target_mask(const MmContext *__context) noexcept {
        uint64_t mask = mm_context_active_cpu_mask(__context);
        for (cpu::CpuId id = 0; id < cpu::MAX_CPUS; id++) {
            const uint64_t bit = cpu_bit(id);
            if ((mask & bit) == 0)
                continue;
            if (!cpu::cpu_online(id) || cpu::slot_for(id).timer_state != cpu::LocalTimerState::Ready)
                mask &= ~bit;
        }
        return mask;
    }
}   // namespace mm
NAMESPACE_BIGOS_END
