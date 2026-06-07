#ifndef _BIG_PROC_H
#define _BIG_PROC_H

#include <bigos/types.h>

namespace bigos::proc {
    constexpr uint64_t USER_CODE_BASE = 0x0000000000400000ull;
    constexpr uint64_t USER_DATA_BASE = 0x0000000000401000ull;
    constexpr uint64_t USER_STACK_TOP = 0x0000000000800000ull;
    constexpr uint64_t USER_STACK_PAGES = 1;

    enum class ProcessState : uint8_t {
        Empty = 0,
        Created,
        Running,
        Terminated,
        Faulted,
        Reaped,
    };

    struct UserRange {
        uint64_t base;
        uint64_t len;
    };

    struct Process {
        uint32_t pid;
        uint64_t address_space_root;
        uint64_t kernel_address_space_root;
        uint64_t entry;
        UserRange code;
        UserRange data;
        UserRange stack;
        uint64_t code_phys;
        uint64_t data_phys;
        uint64_t stack_phys;
        void *kernel_stack_base;
        uint64_t kernel_stack_len;
        uint64_t kernel_stack_top;
        ProcessState state;
        bool reap_pending;
        bool resources_reclaimed;
        int64_t exit_code;
        int64_t fault_reason;
    };

    bool create_first_user_process(Process *__process) noexcept;
    [[noreturn]] void run_user_process(Process *__process) noexcept;
    Process *current_process() noexcept;
    bool validate_user_buffer(uint64_t __addr, uint64_t __len) noexcept;
    void mark_current_faulted(int64_t __reason) noexcept;
    [[noreturn]] void fault_current_and_exit(int64_t __reason) noexcept;
    [[noreturn]] void exit_current(int64_t __code) noexcept;
    void reap_pending_processes() noexcept;

#ifdef BIGOS_USER_PROGRAM_SMOKE
    void user_program_smoke_entry(void *) noexcept;
#endif
}   // namespace bigos::proc

#endif   // _BIG_PROC_H
