#ifndef _BIG_PERCPU_H
#define _BIG_PERCPU_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace cpu {
    using CpuId = uint32_t;

    constexpr CpuId BOOTSTRAP_CPU_ID = 0;
    constexpr CpuId MAX_CPUS = 8;
    constexpr uint32_t MAX_IOAPICS = 4;
    constexpr uint32_t INVALID_APIC_ID = 0xffffffffu;

    enum class CpuRole : uint8_t {
        Bootstrap,
        Application,
    };

    enum class CpuStartupState : uint8_t {
        Empty,
        Discovered,
        Online,
        Offline,
        Failed,
    };

    enum class CpuFailureReason : uint8_t {
        None,
        UnsupportedCpuId,
        MissingTopology,
        InvalidTopology,
        DuplicateApicId,
        CapacityExceeded,
        ApicUnavailable,
        StartupTimeout,
        TimerUnavailable,
    };

    enum class LocalTimerState : uint8_t {
        Disabled,
        Calibrating,
        Ready,
        Failed,
    };

    enum class TopologyState : uint8_t {
        BootstrapOnly,
        MpTableLoaded,
        MpTableMissing,
        MpTableInvalid,
        AcpiMadtLoaded,
        AcpiMadtMissing,
        AcpiMadtInvalid,
        CapacityExceeded,
    };

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

    struct CpuSlot {
        CpuId id;
        uint32_t apic_id;
        CpuRole role;
        CpuStartupState startup_state;
        CpuFailureReason failure;
        LocalTimerState timer_state;
        uint64_t timer_ticks;
        LocalState local;
    };

    struct IoApicDescriptor {
        uint8_t id;
        uint8_t version;
        bool enabled;
        uint32_t address;
    };

    void init_bootstrap_cpu() noexcept;
    void init_topology_from_mp() noexcept;
    CpuId cpu_capacity() noexcept;
    CpuId online_cpu_count() noexcept;
    CpuId discovered_cpu_count() noexcept;
    TopologyState topology_state() noexcept;
    uint32_t ioapic_count() noexcept;
    const CpuSlot &slot_for(CpuId __id) noexcept;
    const IoApicDescriptor &ioapic_for(uint32_t __index) noexcept;
    CpuId current_cpu_id() noexcept;
    bool is_bootstrap_cpu() noexcept;
    bool cpu_id_supported(CpuId __id) noexcept;
    bool cpu_online(CpuId __id) noexcept;
    bool mark_cpu_online(CpuId __id, uint32_t __apic_id) noexcept;
    bool mark_cpu_failed(CpuId __id, CpuFailureReason __reason) noexcept;
    bool set_local_timer_state(CpuId __id, LocalTimerState __state) noexcept;
    bool record_local_timer_tick(CpuId __id) noexcept;
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
