#ifndef _ARCH_X86_AP_STARTUP_H
#define _ARCH_X86_AP_STARTUP_H

#include <bigos/types.h>

#define BIGOS_AP_STARTUP_OWNER "x86-ap-startup"

NAMESPACE_BIGOS_BEG
namespace arch::x86::ap_startup {
    constexpr uint32_t TRAMPOLINE_PHYS = 0x00007000u;
    constexpr uint32_t TRAMPOLINE_SIZE = 0x00001000u;
    constexpr uint32_t MAILBOX_PHYS = 0x00008000u;
    constexpr uint32_t MAILBOX_SIZE = 0x00001000u;
    constexpr uint32_t RESERVED_PHYS = TRAMPOLINE_PHYS;
    constexpr uint32_t RESERVED_SIZE = TRAMPOLINE_SIZE + MAILBOX_SIZE;

    constexpr uint32_t MAILBOX_MAGIC = 0x41504d42u;   // "BMPA"
    constexpr uint32_t MAILBOX_VERSION = 1;
    constexpr uint32_t INVALID_CPU_ID = 0xffffffffu;
    constexpr uint32_t ACK_UNSTARTED = 0;
    constexpr uint32_t ACK_ONLINE = 1;
    constexpr uint32_t ACK_INVALID_MAILBOX = 2;
    constexpr uint32_t ACK_INVALID_CPU = 3;

    enum class PrepareState : uint32_t {
        Unprepared = 0,
        Prepared = 1,
        InvalidDirectMap = 2,
        TrampolineTooLarge = 3,
        InvalidCpu = 4,
    };

    enum class WaitResult : uint32_t {
        Online = 0,
        Timeout = 1,
        InvalidCpu = 2,
        InvalidMailbox = 3,
    };

    struct DescriptorPointer64 {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed));

    struct StartupMailbox {
        uint32_t magic;
        uint32_t version;
        uint32_t size;
        uint32_t cpu_id;
        uint32_t apic_id;
        uint32_t flags;
        uint64_t kernel_cr3;
        DescriptorPointer64 gdtr;
        DescriptorPointer64 idtr;
        uint64_t tss_rsp0;
        uint64_t ap_stack_top;
        uint64_t entry_point;
        uint64_t trampoline_phys;
        uint64_t trampoline_size;
        uint32_t ack_state;
        uint32_t reserved0;
    } __attribute__((packed));

    bool prepare_trampoline_region() noexcept;
    bool prepare_startup_mailbox(uint32_t __cpu_id, uint32_t __apic_id, uint64_t __ap_stack_top,
        uint64_t __entry_point, uint64_t __tss_rsp0 = 0) noexcept;
    WaitResult wait_for_online_ack(uint32_t __cpu_id, uint32_t __timeout_iterations) noexcept;
    bool init_local_timer_for_cpu(uint32_t __cpu_id) noexcept;
    uint32_t start_application_processors() noexcept;
    PrepareState prepare_state() noexcept;
    const StartupMailbox *mailbox() noexcept;
}   // namespace arch::x86::ap_startup
NAMESPACE_BIGOS_END

extern "C" [[noreturn]] void bigos_x86_ap_kernel_entry(
    bigos::arch::x86::ap_startup::StartupMailbox *__mailbox) noexcept;

#endif   // _ARCH_X86_AP_STARTUP_H
