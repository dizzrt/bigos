#include <bigos/proc.h>

#include <string.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/panic.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#include <bigos/user_mode.h>

#include "../../mm/buddy.h"
#include "../../mm/memdef.h"

namespace {
    constexpr uint32_t FIRST_PID = 1;
    constexpr uint32_t PROCESS_KERNEL_STACK_PAGES = 1;

    // Flat embedded image selected for stage 6: it avoids any kernel FS/block
    // dependency and still exercises code, data/BSS, stack, syscall write, exit.
    // The inline payload writes the deterministic BIGOS_USER_WRITE marker.
    constexpr uint8_t FIRST_USER_CODE[] = {
        0x48, 0xc7, 0xc0, 0x02, 0x00, 0x00, 0x00,               // mov $SYS_WRITE,%rax
        0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00,               // mov $1,%rdi
        0x48, 0x8d, 0x35, 0x18, 0x00, 0x00, 0x00,               // lea message(%rip),%rsi
        0x48, 0xc7, 0xc2, 0x11, 0x00, 0x00, 0x00,               // mov $17,%rdx
        0xcd, 0x80,                                             // int $0x80
        0x48, 0xc7, 0xc0, 0x03, 0x00, 0x00, 0x00,               // mov $SYS_EXIT,%rax
        0x48, 0x31, 0xff,                                       // xor %rdi,%rdi
        0xcd, 0x80,                                             // int $0x80
        0xf4, 0xeb, 0xfd,                                       // hlt; jmp .
        'B',  'I',  'G',  'O',  'S',  '_',  'U',  'S',  'E',
        'R',  '_',  'W',  'R',  'I',  'T',  'E',  '\n',
    };

    bigos::proc::Process *g_current_process = nullptr;
    bigos::proc::Process *g_reap_pending_process = nullptr;
    bool g_user_mode_initialized = false;

    uint64_t alloc_user_frame() noexcept {
        return (uint64_t)bigos::mm::__detail::alloc_physical_order(0, 0);
    }

    void free_user_frame(uint64_t __phys) noexcept {
        if (__phys != 0)
            bigos::mm::__detail::free_physical_order((void *)__phys);
    }

    bool zero_frame(uint64_t __phys) noexcept {
        void *direct = bigos::mm::phys_to_direct(__phys);
        if (direct == nullptr)
            return false;
        memset(direct, 0, PAGE_SIZE);
        return true;
    }

    bool copy_to_frame(uint64_t __phys, const void *__src, uint64_t __len) noexcept {
        void *direct = bigos::mm::phys_to_direct(__phys);
        if (direct == nullptr || __len > PAGE_SIZE)
            return false;
        memset(direct, 0, PAGE_SIZE);
        memcpy(direct, __src, __len);
        return true;
    }

    [[noreturn]] void halt_failed(const char *__marker) noexcept {
        bigos::serial_puts(__marker);
        bigos::khalt();
    }

    uint64_t current_stack_pointer() noexcept {
        uint64_t rsp;
        asm volatile("movq %%rsp, %0" : "=r"(rsp));
        return rsp;
    }

    bool stack_is_active(const bigos::proc::Process *process) noexcept {
        if (process == nullptr || process->kernel_stack_base == nullptr)
            return false;
        const uint64_t rsp = current_stack_pointer();
        const uint64_t base = (uint64_t)process->kernel_stack_base;
        return rsp >= base && rsp < base + process->kernel_stack_len;
    }

    void mark_reap_pending(bigos::proc::Process *process) noexcept {
        process->reap_pending = true;
        g_reap_pending_process = process;
    }
}   // namespace

namespace bigos::proc {
    bool create_first_user_process(Process *__process) noexcept {
        if (__process == nullptr || sizeof(FIRST_USER_CODE) > PAGE_SIZE)
            return false;

        memset(__process, 0, sizeof(*__process));
        __process->pid = FIRST_PID;
        __process->address_space_root = bigos::mm::derive_user_address_space_root();
        __process->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (__process->address_space_root == bigos::mm::INVALID_PHYS_ADDR)
            return false;

        uint64_t code_phys = alloc_user_frame();
        uint64_t data_phys = alloc_user_frame();
        uint64_t stack_phys = alloc_user_frame();
        bool code_mapped = false;
        bool data_mapped = false;
        bool stack_mapped = false;
        const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
        if (code_phys == 0 || data_phys == 0 || stack_phys == 0)
            goto fail;
        if (!copy_to_frame(code_phys, FIRST_USER_CODE, sizeof(FIRST_USER_CODE)) || !zero_frame(data_phys) ||
            !zero_frame(stack_phys))
            goto fail;

        if (!bigos::mm::map_page_in_root(
                __process->address_space_root, USER_CODE_BASE, code_phys, bigos::mm::page_attr::USER_CODE))
            goto fail;
        code_mapped = true;
        if (!bigos::mm::map_page_in_root(
                __process->address_space_root, USER_DATA_BASE, data_phys, bigos::mm::page_attr::USER_DATA))
            goto fail;
        data_mapped = true;
        if (!bigos::mm::map_page_in_root(
                __process->address_space_root, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA))
            goto fail;
        stack_mapped = true;

        __process->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (__process->kernel_stack_base == nullptr)
            goto fail;

        __process->code_phys = code_phys;
        __process->data_phys = data_phys;
        __process->stack_phys = stack_phys;
        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top =
            (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        __process->entry = USER_CODE_BASE;
        __process->code = {USER_CODE_BASE, PAGE_SIZE};
        __process->data = {USER_DATA_BASE, PAGE_SIZE};
        __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
        return true;

    fail:
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        if (!code_mapped)
            free_user_frame(code_phys);
        if (!data_mapped)
            free_user_frame(data_phys);
        if (!stack_mapped)
            free_user_frame(stack_phys);
        memset(__process, 0, sizeof(*__process));
        __process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        return false;
    }

    [[noreturn]] void run_user_process(Process *__process) noexcept {
        if (__process == nullptr || __process->state != ProcessState::Created)
            halt_failed("BIGOS_USER_LOAD_FAILED invalid-process\n");

        if (!g_user_mode_initialized) {
            bigos::arch::x86::init_user_mode();
            g_user_mode_initialized = true;
        }

        g_current_process = __process;
        __process->state = ProcessState::Running;
        __process->kernel_address_space_root = bigos::mm::read_cr3();
        bigos::arch::x86::set_tss_rsp0(__process->kernel_stack_top);
        bigos::mm::activate_address_space_root(__process->address_space_root);
        bigos::serial_puts("BIGOS_USER_ENTER\n");
        bigos::arch::x86::enter_user_mode(__process->entry, __process->stack.base + __process->stack.len);
    }

    Process *current_process() noexcept {
        return g_current_process;
    }

    bool validate_user_buffer(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_WRITE_MAX_LEN)
            return false;
        return bigos::mm::user_range_mapped(process->address_space_root, __addr, __len);
    }

    void mark_current_faulted(int64_t __reason) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            return;
        process->state = ProcessState::Faulted;
        process->exit_code = __reason;
        process->fault_reason = __reason;
        mark_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_PAGE_FAULT\n");
        if (process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::mm::activate_address_space_root(process->kernel_address_space_root);
    }

    [[noreturn]] void fault_current_and_exit(int64_t __reason) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            halt_failed("BIGOS_USER_FAULT_FAILED no-process\n");

        mark_current_faulted(__reason);
        g_current_process = nullptr;
        bigos::sched::thread_exit();
    }

    [[noreturn]] void exit_current(int64_t __code) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            halt_failed("BIGOS_USER_EXIT_FAILED no-process\n");

        process->state = ProcessState::Terminated;
        process->exit_code = __code;
        mark_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_EXIT\n");
        if (process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::mm::activate_address_space_root(process->kernel_address_space_root);
        g_current_process = nullptr;
        bigos::sched::thread_exit();
    }

    void reap_pending_processes() noexcept {
        Process *process = g_reap_pending_process;
        if (process == nullptr || !process->reap_pending || process->resources_reclaimed)
            return;
        if (stack_is_active(process)) {
            bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-stack\n");
            return;
        }
        if (process->address_space_root == (bigos::mm::read_cr3() & 0x000ffffffffff000ull)) {
            bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-root\n");
            return;
        }

        if (!bigos::mm::teardown_user_address_space(process->address_space_root)) {
            bigos::serial_puts("BIGOS_USER_REAP_FAILED address-space\n");
            return;
        }
        process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;

        if (process->kernel_stack_base != nullptr) {
            bigos::free_pages(process->kernel_stack_base);
            process->kernel_stack_base = nullptr;
            process->kernel_stack_len = 0;
            process->kernel_stack_top = 0;
        }

        process->resources_reclaimed = true;
        process->reap_pending = false;
        process->state = ProcessState::Reaped;
        g_reap_pending_process = nullptr;
        bigos::serial_puts("BIGOS_USER_RECLAIMED\n");
    }

#ifdef BIGOS_USER_PROGRAM_SMOKE
    void user_program_smoke_entry(void *) noexcept {
        static Process first_process;
        if (!create_first_user_process(&first_process))
            halt_failed("BIGOS_USER_LOAD_FAILED create\n");
        run_user_process(&first_process);
    }
#endif
}   // namespace bigos::proc
