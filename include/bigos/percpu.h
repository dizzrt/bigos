#ifndef _BIG_PERCPU_H
#define _BIG_PERCPU_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace cpu {
    using CpuId = uint32_t;

    constexpr CpuId BOOTSTRAP_CPU_ID = 0;
    constexpr CpuId MAX_CPUS = 1;

    struct LocalState {
        CpuId id;
        void *current_thread;
        void *current_process;
        uint64_t current_address_space_root;
        uint32_t irq_nesting_depth;
        uint32_t nonblocking_depth;
        uint32_t preemption_disable_depth;
        bool reschedule_pending;
    };

    CpuId current_cpu_id() noexcept;
    bool is_bootstrap_cpu() noexcept;
    bool cpu_id_supported(CpuId __id) noexcept;
    LocalState &current_state() noexcept;
    LocalState &state_for(CpuId __id) noexcept;

    void set_current_thread(void *__t) noexcept;
    void set_current_process(void *__process) noexcept;
    void set_current_address_space_root(uint64_t __root_phys) noexcept;
    void set_irq_nesting_depth(uint32_t __depth) noexcept;
    void set_nonblocking_depth(uint32_t __depth) noexcept;
    void set_preemption_disable_depth(uint32_t __depth) noexcept;
    void set_reschedule_pending(bool __pending) noexcept;
}   // namespace cpu
NAMESPACE_BIGOS_END

#endif   // _BIG_PERCPU_H
