#include <bigos/percpu.h>
#include <bigos/panic.h>

NAMESPACE_BIGOS_BEG
namespace cpu {
    namespace {
        LocalState g_bootstrap_state = {
            BOOTSTRAP_CPU_ID,
            nullptr,
            nullptr,
            0,
            0,
            0,
            0,
            false,
        };

        [[noreturn]] void unsupported_cpu_access(CpuId __id) noexcept {
            bigos::kpanic(bigos::PanicCode::Generic, "cpu-local", "unsupported cpu id %u\n", __id);
        }
    }   // namespace

    CpuId current_cpu_id() noexcept {
        return BOOTSTRAP_CPU_ID;
    }

    bool is_bootstrap_cpu() noexcept {
        return current_cpu_id() == BOOTSTRAP_CPU_ID;
    }

    bool cpu_id_supported(CpuId __id) noexcept {
        return __id == BOOTSTRAP_CPU_ID;
    }

    LocalState &state_for(CpuId __id) noexcept {
        if (!cpu_id_supported(__id))
            unsupported_cpu_access(__id);
        return g_bootstrap_state;
    }

    LocalState &current_state() noexcept {
        return state_for(current_cpu_id());
    }

    void set_current_thread(void *__t) noexcept {
        current_state().current_thread = __t;
    }

    void set_current_process(void *__process) noexcept {
        current_state().current_process = __process;
    }

    void set_current_address_space_root(uint64_t __root_phys) noexcept {
        current_state().current_address_space_root = __root_phys;
    }

    void set_irq_nesting_depth(uint32_t __depth) noexcept {
        current_state().irq_nesting_depth = __depth;
    }

    void set_nonblocking_depth(uint32_t __depth) noexcept {
        current_state().nonblocking_depth = __depth;
    }

    void set_preemption_disable_depth(uint32_t __depth) noexcept {
        current_state().preemption_disable_depth = __depth;
    }

    void set_reschedule_pending(bool __pending) noexcept {
        current_state().reschedule_pending = __pending;
    }
}   // namespace cpu
NAMESPACE_BIGOS_END
