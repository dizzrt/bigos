#include <bigos/proc.h>

#include <string.h>
#include <bigos/arch_vm_user_boundary.h>
#include <bigos/cred.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/percpu.h>
#include <bigos/panic.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#include <bigos/time.h>
#include <bigos/tty.h>
#include <irq/interrupt.h>

#ifdef BIGOS_NONBLOCKING_FD_SMOKE
#include <bigos/ipc/pipe.h>
#include <bigos/net/socket.h>
#endif

#include "../../mm/buddy.h"
#include "../../mm/memdef.h"
#include "../../mm/vmem.h"

namespace bigos::proc {
    bool materialize_file_backed_page(Process *__process, uint64_t __page, VmaEntry *__vma) noexcept;
}

namespace {
    constexpr uint32_t PROCESS_KERNEL_STACK_PAGES = 1;
    constexpr uint32_t ELF_MAX_LOAD_SEGMENTS = 8;
    constexpr uint32_t ELF_MAX_TRACKED_PAGES = 32;
    constexpr uint64_t USER_INITIAL_STACK_ALIGN = 16;
    constexpr uint64_t ELF_MAGIC = 0x464c457full;
    constexpr uint16_t ELF_TYPE_EXEC = 2;
    constexpr uint16_t ELF_TYPE_DYN = 3;
    constexpr uint16_t ELF_MACHINE_X86_64 = 62;
    constexpr uint32_t ELF_VERSION_CURRENT = 1;
    constexpr uint32_t PT_LOAD = 1;
    constexpr uint32_t PT_DYNAMIC = 2;
    constexpr uint32_t PT_INTERP = 3;
    constexpr uint32_t PT_NOTE = 4;
    constexpr uint32_t PT_PHDR = 6;
    constexpr uint32_t PT_TLS = 7;
    constexpr uint32_t PF_X = 1;
    constexpr uint32_t PF_W = 2;
    constexpr uint32_t PF_R = 4;
    constexpr uint32_t MAX_SHARED_READONLY_PAGES = 64;

    // Flat embedded image selected for the first-user-program smoke path: it
    // avoids any kernel FS/block dependency and still exercises code, data/BSS,
    // stack, syscall write, and exit.
    // The inline payload writes the deterministic BIGOS_USER_WRITE marker.
    constexpr uint8_t FIRST_USER_CODE[] = {
        0x48,
        0xc7,
        0xc0,
        0x02,
        0x00,
        0x00,
        0x00,   // mov $SYS_WRITE,%rax
        0x48,
        0xc7,
        0xc7,
        0x01,
        0x00,
        0x00,
        0x00,   // mov $1,%rdi
        0x48,
        0x8d,
        0x35,
        0x18,
        0x00,
        0x00,
        0x00,   // lea message(%rip),%rsi
        0x48,
        0xc7,
        0xc2,
        0x11,
        0x00,
        0x00,
        0x00,   // mov $17,%rdx
        0xcd,
        0x80,   // int $0x80
        0x48,
        0xc7,
        0xc0,
        0x03,
        0x00,
        0x00,
        0x00,   // mov $SYS_EXIT,%rax
        0x48,
        0x31,
        0xff,   // xor %rdi,%rdi
        0xcd,
        0x80,   // int $0x80
        0xf4,
        0xeb,
        0xfd,   // hlt; jmp .
        'B',
        'I',
        'G',
        'O',
        'S',
        '_',
        'U',
        'S',
        'E',
        'R',
        '_',
        'W',
        'R',
        'I',
        'T',
        'E',
        '\n',
    };

    // Bootstrap-CPU current process slot. Accesses still use this single-core
    // storage today, while updates are mirrored into the CPU-local state boundary
    // so future SMP work can split it per CPU without changing proc callers.
    bigos::proc::Process *g_current_process = nullptr;
    uint64_t g_kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;

    void set_current_process_slot(bigos::proc::Process *__process) noexcept {
        g_current_process = __process;
        bigos::cpu::set_current_process(__process);
    }

    bigos::proc::Process *current_process_slot() noexcept {
        return (bigos::proc::Process *)bigos::cpu::current_state().current_process;
    }

    bool attach_mm_context(bigos::proc::Process *__process, uint64_t __root) noexcept {
        if (__process == nullptr || __root == bigos::mm::INVALID_PHYS_ADDR)
            return false;
        if (__process->mm_context != nullptr)
            bigos::mm::release_mm_context(__process->mm_context);
        __process->mm_context = bigos::mm::create_mm_context(__root);
        if (__process->mm_context == nullptr)
            return false;
        __process->address_space_root = bigos::mm::mm_context_root(__process->mm_context);
        return true;
    }

    void forget_mm_context(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        if (__process->mm_context != nullptr)
            bigos::mm::release_mm_context(__process->mm_context);
        __process->mm_context = nullptr;
        __process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
    }

    bigos::mm::MmContext *ensure_mm_context(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return nullptr;
        if (__process->mm_context == nullptr && __process->address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            (void)attach_mm_context(__process, __process->address_space_root);
        return __process->mm_context;
    }

    bool activate_process_user_address_space(bigos::proc::Process *__process) noexcept {
        bigos::mm::MmContext *context = ensure_mm_context(__process);
        if (context == nullptr)
            return false;
        if (!bigos::mm::enter_mm_context(context))
            return false;
        bigos::arch::vm_user::activate_user_address_space(bigos::mm::mm_context_root(context));
#ifdef BIGOS_TLB_SHOOTDOWN_SMOKE
        bigos::serial_puts("BIGOS_MM_CONTEXT_RESIDENT\n");
#endif
        return true;
    }

    void activate_process_kernel_address_space(bigos::proc::Process *__process) noexcept {
        if (__process != nullptr && __process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
            __process->kernel_address_space_root != __process->address_space_root)
            bigos::arch::vm_user::activate_kernel_address_space(__process->kernel_address_space_root);
        else if (g_kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::arch::vm_user::activate_kernel_address_space(g_kernel_address_space_root);
        bigos::mm::leave_current_mm_context();
    }

    bool path_equal(const char *__a, const char *__b) noexcept {
        if (__a == nullptr || __b == nullptr)
            return false;
        size_t i = 0;
        while (__a[i] != 0 && __b[i] != 0) {
            if (__a[i] != __b[i])
                return false;
            i++;
        }
        return __a[i] == __b[i];
    }

    bool starts_with_dotdot_component(const char *__path) noexcept {
        return __path != nullptr && __path[0] == '.' && __path[1] == '.' && (__path[2] == 0 || __path[2] == '/');
    }

    // Intrusive process registry: a singly-anchored doubly linked list head plus
    // a live count bounded by MAX_PROCESSES_SOFT_LIMIT. Replaces the former fixed
    // g_process_table[MAX_PROCESSES] array; nodes are embedded in each Process.
    bigos::proc::Process *g_process_list_head = nullptr;
    uint32_t g_process_count = 0;
    bigos::proc::Process *g_init_process = nullptr;
    uint32_t g_reap_head_pid = 0;
    uint32_t g_next_pid = 1;
    bool g_user_mode_initialized = false;
    bigos::sched::WaitQueue g_process_wait_queue = {};
    bool g_proc_initialized = false;

#ifdef BIGOS_DEMAND_PAGING_SMOKE
    // Smoke-only, default-off allocation-failure injection. Forces
    // alloc_user_frame() to fail so the demand-paging smoke can exercise the
    // deterministic allocation-failure kill path without a global test macro.
    bool g_demand_paging_force_alloc_fail = false;
#endif

#ifdef BIGOS_GROWABLE_TABLES_SMOKE
    // Smoke-only, default-off allocation-failure injection. Forces
    // alloc_process_object() and grow_fd_table() to fail so the growable-tables
    // smoke can exercise the deterministic degradation paths without a global
    // test macro.
    bool g_growable_force_alloc_fail = false;
#endif

#ifdef BIGOS_FORK_COW_SMOKE
    // Smoke-only, default-off allocation-failure injection. Forces
    // alloc_user_frame() to fail so the fork/COW smoke can exercise the
    // deterministic degradation paths (fork rollback, write-split kill) without a
    // global test macro.
    bool g_fork_cow_force_alloc_fail = false;
#endif

    uint64_t alloc_user_frame() noexcept {
#ifdef BIGOS_DEMAND_PAGING_SMOKE
        if (g_demand_paging_force_alloc_fail)
            return 0;
#endif
#ifdef BIGOS_FORK_COW_SMOKE
        if (g_fork_cow_force_alloc_fail)
            return 0;
#endif
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

    bool add_overflow(uint64_t __a, uint64_t __b, uint64_t *__out) noexcept {
        const uint64_t result = __a + __b;
        if (result < __a)
            return true;
        *__out = result;
        return false;
    }

    uint64_t page_down(uint64_t __value) noexcept {
        return __value & ~(PAGE_SIZE - 1);
    }

    bool page_up(uint64_t __value, uint64_t *__out) noexcept {
        uint64_t with_padding = 0;
        if (add_overflow(__value, PAGE_SIZE - 1, &with_padding))
            return false;
        *__out = page_down(with_padding);
        return true;
    }

    bool is_power_of_two(uint64_t __value) noexcept {
        return __value != 0 && (__value & (__value - 1)) == 0;
    }

    bool ranges_overlap(uint64_t __a_base, uint64_t __a_len, uint64_t __b_base, uint64_t __b_len) noexcept {
        uint64_t a_end = 0;
        uint64_t b_end = 0;
        if (add_overflow(__a_base, __a_len, &a_end) || add_overflow(__b_base, __b_len, &b_end))
            return true;
        return __a_base < b_end && __b_base < a_end;
    }

    bool user_range_valid(uint64_t __base, uint64_t __len) noexcept {
        if (__len == 0)
            return false;
        uint64_t end = 0;
        if (add_overflow(__base, __len, &end))
            return false;
        return end <= bigos::proc::USER_LOW_HALF_LIMIT;
    }

    bool page_aligned(uint64_t __value) noexcept {
        return (__value & (PAGE_SIZE - 1)) == 0;
    }

    uint8_t permissions_value(bigos::proc::VmaPermission __perm) noexcept {
        return (uint8_t)__perm;
    }

    bool permissions_include(bigos::proc::VmaPermission __have, bigos::proc::VmaPermission __need) noexcept {
        return (permissions_value(__have) & permissions_value(__need)) == permissions_value(__need);
    }

    bool permissions_wx(bigos::proc::VmaPermission __perm) noexcept {
        return permissions_include(__perm, bigos::proc::VmaPermission::Write) &&
               permissions_include(__perm, bigos::proc::VmaPermission::Execute);
    }

    bigos::proc::VmaPermission segment_permissions(uint32_t __flags) noexcept {
        uint8_t perm = permissions_value(bigos::proc::VmaPermission::Read);
        if ((__flags & PF_W) != 0)
            perm |= permissions_value(bigos::proc::VmaPermission::Write);
        if ((__flags & PF_X) != 0)
            perm |= permissions_value(bigos::proc::VmaPermission::Execute);
        return (bigos::proc::VmaPermission)perm;
    }

    bool vma_range_bounds(uint64_t __start, uint64_t __end) noexcept {
        return __start < __end && page_aligned(__start) && page_aligned(__end) &&
               __end <= bigos::proc::USER_LOW_HALF_LIMIT;
    }

    bool range_inside(uint64_t __start, uint64_t __end, const bigos::proc::UserRange &__range) noexcept {
        uint64_t range_end = 0;
        if (__range.len == 0 || add_overflow(__range.base, __range.len, &range_end))
            return false;
        return __start >= __range.base && __end <= range_end;
    }

    bool range_inside_pair(uint64_t __start, uint64_t __end, const bigos::proc::UserRange &__low,
        const bigos::proc::UserRange &__high) noexcept {
        uint64_t combined_end = 0;
        if (__low.len == 0 || __high.len == 0 || add_overflow(__high.base, __high.len, &combined_end))
            return false;
        return __start >= __low.base && __end <= combined_end && __low.base < __high.base;
    }

    bool range_overlaps_user_range(uint64_t __start, uint64_t __end, const bigos::proc::UserRange &__range) noexcept {
        if (__range.len == 0)
            return false;
        return ranges_overlap(__start, __end - __start, __range.base, __range.len);
    }

    bool runtime_layout_valid(const bigos::proc::UserRuntimeLayout &__layout) noexcept {
        const bigos::proc::UserRange ranges[] = {
            __layout.elf_load,
            __layout.heap,
            __layout.anonymous,
            __layout.stack_guard,
            __layout.stack_growth,
            __layout.stack,
            __layout.future_runtime,
            __layout.file_mapped,
        };
        for (uint32_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
            if (ranges[i].len == 0 || !user_range_valid(ranges[i].base, ranges[i].len) ||
                !page_aligned(ranges[i].base) || !page_aligned(ranges[i].len))
                return false;
            for (uint32_t j = i + 1; j < sizeof(ranges) / sizeof(ranges[0]); j++) {
                if (range_overlaps_user_range(ranges[i].base, ranges[i].base + ranges[i].len, ranges[j]))
                    return false;
            }
        }
        // Default-off bounded dynamic-link path: the live interpreter and
        // shared-object regions sit inside the reserved future-runtime gap and
        // MUST stay page-aligned, in-range, non-overlapping with each other and
        // with every static range above.
        if (__layout.dynamic) {
            const bigos::proc::UserRange dyn_ranges[] = {__layout.dyn_interp, __layout.dyn_lib};
            for (uint32_t i = 0; i < sizeof(dyn_ranges) / sizeof(dyn_ranges[0]); i++) {
                if (dyn_ranges[i].len == 0 || !user_range_valid(dyn_ranges[i].base, dyn_ranges[i].len) ||
                    !page_aligned(dyn_ranges[i].base) || !page_aligned(dyn_ranges[i].len))
                    return false;
                if (!range_inside(dyn_ranges[i].base, dyn_ranges[i].base + dyn_ranges[i].len, __layout.future_runtime))
                    return false;
                for (uint32_t j = i + 1; j < sizeof(dyn_ranges) / sizeof(dyn_ranges[0]); j++) {
                    if (range_overlaps_user_range(
                            dyn_ranges[i].base, dyn_ranges[i].base + dyn_ranges[i].len, dyn_ranges[j]))
                        return false;
                }
            }
        }
        return true;
    }

    bool init_runtime_layout(bigos::proc::Process *__process, uint64_t __elf_low, uint64_t __elf_high,
        uint64_t __heap_base, bool __dynamic = false) noexcept {
        if (__process == nullptr || !page_aligned(__elf_low) || !page_aligned(__elf_high) || __elf_low >= __elf_high)
            return false;
        uint64_t heap_end = 0;
        if (add_overflow(__heap_base, bigos::proc::USER_HEAP_MAX_PAGES * PAGE_SIZE, &heap_end))
            return false;
        const uint64_t stack_base = bigos::proc::USER_STACK_TOP - bigos::proc::USER_STACK_PAGES * PAGE_SIZE;
        const uint64_t stack_growth_base = stack_base - bigos::proc::USER_STACK_GROWTH_PAGES * PAGE_SIZE;
        const uint64_t stack_guard_base = stack_growth_base - bigos::proc::USER_STACK_GUARD_PAGES * PAGE_SIZE;
        if (__elf_high > __heap_base || heap_end > stack_guard_base ||
            bigos::proc::USER_RUNTIME_RESERVED_BASE > bigos::proc::USER_RUNTIME_RESERVED_END)
            return false;

        bigos::proc::UserRuntimeLayout layout = {};
        layout.elf_load = {__elf_low, __elf_high - __elf_low};
        layout.heap = {__heap_base, heap_end - __heap_base};
        layout.stack_guard = {stack_guard_base, bigos::proc::USER_STACK_GUARD_PAGES * PAGE_SIZE};
        layout.stack_growth = {stack_growth_base, bigos::proc::USER_STACK_GROWTH_PAGES * PAGE_SIZE};
        layout.stack = {stack_base, bigos::proc::USER_STACK_PAGES * PAGE_SIZE};
        layout.arguments = layout.stack;
        layout.future_runtime = {
            bigos::proc::USER_RUNTIME_RESERVED_BASE,
            bigos::proc::USER_RUNTIME_RESERVED_END - bigos::proc::USER_RUNTIME_RESERVED_BASE,
        };
        layout.anonymous = {bigos::proc::USER_ANON_BASE, bigos::proc::USER_ANON_MAX_PAGES * PAGE_SIZE};
        layout.file_mapped = {bigos::proc::USER_FILEMAP_BASE, bigos::proc::USER_FILEMAP_MAX_PAGES * PAGE_SIZE};
        if (__dynamic) {
            layout.dynamic = true;
            layout.dyn_interp = {
                bigos::proc::USER_INTERP_BASE, bigos::proc::USER_INTERP_MAX_PAGES * PAGE_SIZE};
            layout.dyn_lib = {bigos::proc::USER_DYN_LIB_BASE, bigos::proc::USER_DYN_LIB_MAX_PAGES * PAGE_SIZE};
        }
        layout.committed = true;
        if (!runtime_layout_valid(layout))
            return false;
        __process->runtime_layout = layout;
        return true;
    }

    bool runtime_vma_allowed(const bigos::proc::Process *__process, const bigos::proc::VmaEntry &__entry) noexcept {
        if (__process == nullptr || !__process->runtime_layout.committed ||
            !vma_range_bounds(__entry.start, __entry.end))
            return false;
        if (__entry.backing != bigos::proc::VmaBacking::Guard &&
            (__entry.materialized_start < __entry.start || __entry.materialized_start > __entry.end ||
                __entry.materialized_end < __entry.start || __entry.materialized_end > __entry.end))
            return false;

        const auto &layout = __process->runtime_layout;
        switch (__entry.purpose) {
            case bigos::proc::VmaPurpose::Code:
                return range_inside(__entry.start, __entry.end, layout.elf_load) &&
                       __entry.growth == bigos::proc::VmaGrowth::None &&
                       (__entry.backing == bigos::proc::VmaBacking::ElfSegment ||
                           __entry.backing == bigos::proc::VmaBacking::FileBacked ||
                           __entry.backing == bigos::proc::VmaBacking::Anonymous) &&
                       (__entry.backing != bigos::proc::VmaBacking::FileBacked ||
                           (__entry.file_backing != nullptr && page_aligned(__entry.file_offset))) &&
                       permissions_include(__entry.permissions, bigos::proc::VmaPermission::Execute) &&
                       !permissions_include(__entry.permissions, bigos::proc::VmaPermission::Write);
            case bigos::proc::VmaPurpose::Data:
                return range_inside(__entry.start, __entry.end, layout.elf_load) &&
                       __entry.growth == bigos::proc::VmaGrowth::None &&
                       (__entry.backing == bigos::proc::VmaBacking::ElfSegment ||
                           __entry.backing == bigos::proc::VmaBacking::FileBacked ||
                           __entry.backing == bigos::proc::VmaBacking::Anonymous) &&
                       (__entry.backing != bigos::proc::VmaBacking::FileBacked ||
                           (__entry.file_backing != nullptr && page_aligned(__entry.file_offset))) &&
                       !permissions_include(__entry.permissions, bigos::proc::VmaPermission::Execute);
            case bigos::proc::VmaPurpose::Heap:
                return range_inside(__entry.start, __entry.end, layout.heap) &&
                       __entry.backing == bigos::proc::VmaBacking::Anonymous &&
                       __entry.growth == bigos::proc::VmaGrowth::Up &&
                       permissions_include(__entry.permissions, bigos::proc::VmaPermission::Read) &&
                       permissions_include(__entry.permissions, bigos::proc::VmaPermission::Write) &&
                       !permissions_include(__entry.permissions, bigos::proc::VmaPermission::Execute);
            case bigos::proc::VmaPurpose::Anonymous:
                return range_inside(__entry.start, __entry.end, layout.anonymous) &&
                       __entry.backing == bigos::proc::VmaBacking::Anonymous &&
                       __entry.growth == bigos::proc::VmaGrowth::None;
            case bigos::proc::VmaPurpose::Stack:
                return range_inside_pair(__entry.start, __entry.end, layout.stack_growth, layout.stack) &&
                       __entry.backing == bigos::proc::VmaBacking::Anonymous &&
                       __entry.growth == bigos::proc::VmaGrowth::Down &&
                       permissions_include(__entry.permissions, bigos::proc::VmaPermission::Read) &&
                       permissions_include(__entry.permissions, bigos::proc::VmaPermission::Write) &&
                       !permissions_include(__entry.permissions, bigos::proc::VmaPermission::Execute);
            case bigos::proc::VmaPurpose::StackGuard:
                return range_inside(__entry.start, __entry.end, layout.stack_guard) &&
                       __entry.backing == bigos::proc::VmaBacking::Guard &&
                       __entry.growth == bigos::proc::VmaGrowth::None &&
                       __entry.permissions == bigos::proc::VmaPermission::None;
            case bigos::proc::VmaPurpose::FileMapped:
                return range_inside(__entry.start, __entry.end, layout.file_mapped) &&
                       __entry.backing == bigos::proc::VmaBacking::FileBacked &&
                       __entry.growth == bigos::proc::VmaGrowth::None &&
                       __entry.file_backing != nullptr && page_aligned(__entry.file_offset) &&
                       permissions_include(__entry.permissions, bigos::proc::VmaPermission::Read) &&
                       !permissions_include(__entry.permissions, bigos::proc::VmaPermission::Write);
            case bigos::proc::VmaPurpose::DynInterp:
                // Kernel-mapped interpreter (ld.so) ELF segments. Only valid on
                // the dynamic layout, inside the interpreter region, ElfSegment
                // backed, W^X enforced by internal_add_vma.
                return layout.dynamic && range_inside(__entry.start, __entry.end, layout.dyn_interp) &&
                       __entry.backing == bigos::proc::VmaBacking::ElfSegment &&
                       __entry.growth == bigos::proc::VmaGrowth::None;
            case bigos::proc::VmaPurpose::DynLib:
                // ld.so-mapped shared objects: anonymous demand-zero pages inside
                // the shared-object region whose bytes the interpreter copies in.
                return layout.dynamic && range_inside(__entry.start, __entry.end, layout.dyn_lib) &&
                       __entry.backing == bigos::proc::VmaBacking::Anonymous &&
                       __entry.growth == bigos::proc::VmaGrowth::None;
        }
        return false;
    }

    void internal_init_vmas(bigos::proc::VmaCollection *__vmas) noexcept {
        if (__vmas == nullptr)
            return;
        memset(__vmas, 0, sizeof(*__vmas));
        __vmas->anon_next = bigos::proc::USER_ANON_BASE;
        __vmas->filemap_next = bigos::proc::USER_FILEMAP_BASE;
    }

    bool internal_add_vma(bigos::proc::VmaCollection *__vmas, const bigos::proc::VmaEntry &__entry) noexcept {
        if (__vmas == nullptr || !vma_range_bounds(__entry.start, __entry.end) || permissions_wx(__entry.permissions) ||
            __vmas->count >= bigos::proc::MAX_VMAS)
            return false;

        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &existing = __vmas->entries[i];
            if (existing.used && __entry.start < existing.end && existing.start < __entry.end)
                return false;
        }

        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            if (!__vmas->entries[i].used) {
                __vmas->entries[i] = __entry;
                __vmas->entries[i].used = true;
                __vmas->count++;
                return true;
            }
        }
        return false;
    }

    bool internal_add_process_vma(bigos::proc::Process *__process, const bigos::proc::VmaEntry &__entry) noexcept {
        if (!runtime_vma_allowed(__process, __entry))
            return false;
        return internal_add_vma(&__process->vmas, __entry);
    }

    bool internal_remove_vma(bigos::proc::VmaCollection *__vmas, uint64_t __start, uint64_t __end) noexcept {
        if (__vmas == nullptr)
            return false;
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            bigos::proc::VmaEntry &entry = __vmas->entries[i];
            if (entry.used && entry.start == __start && entry.end == __end) {
                memset(&entry, 0, sizeof(entry));
                __vmas->count--;
                return true;
            }
        }
        return false;
    }

    const bigos::proc::VmaEntry *internal_find_vma(const bigos::proc::VmaCollection *__vmas, uint64_t __addr) noexcept {
        if (__vmas == nullptr || __addr >= bigos::proc::USER_LOW_HALF_LIMIT)
            return nullptr;
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &entry = __vmas->entries[i];
            if (entry.used && __addr >= entry.start && __addr < entry.end)
                return &entry;
        }
        return nullptr;
    }

    bigos::proc::VmaEntry *internal_find_vma_mut(bigos::proc::VmaCollection *__vmas, uint64_t __addr) noexcept {
        return (bigos::proc::VmaEntry *)internal_find_vma(__vmas, __addr);
    }

    bool internal_vma_range_allowed(const bigos::proc::VmaCollection *__vmas, uint64_t __addr, uint64_t __len,
        bigos::proc::VmaPermission __perm) noexcept {
        if (__len == 0 || !user_range_valid(__addr, __len))
            return false;

        uint64_t end = 0;
        if (add_overflow(__addr, __len, &end))
            return false;

        uint64_t cursor = __addr;
        while (cursor < end) {
            const bigos::proc::VmaEntry *entry = internal_find_vma(__vmas, cursor);
            if (entry == nullptr || !permissions_include(entry->permissions, __perm))
                return false;
            if (entry->end <= cursor)
                return false;
            cursor = entry->end < end ? entry->end : end;
        }
        return true;
    }

    bool runtime_range_allowed(const bigos::proc::Process *__process, uint64_t __addr, uint64_t __len,
        bigos::proc::VmaPermission __perm) noexcept {
        if (__process == nullptr || __len == 0 || !user_range_valid(__addr, __len))
            return false;

        uint64_t end = 0;
        if (add_overflow(__addr, __len, &end))
            return false;

        uint64_t cursor = __addr;
        while (cursor < end) {
            const bigos::proc::VmaEntry *entry = internal_find_vma(&__process->vmas, cursor);
            if (entry == nullptr || !runtime_vma_allowed(__process, *entry) ||
                !permissions_include(entry->permissions, __perm))
                return false;
            if (entry->end <= cursor)
                return false;
            cursor = entry->end < end ? entry->end : end;
        }
        return true;
    }

    bool internal_vma_attr_allowed(const bigos::proc::VmaEntry *__vma, bigos::mm::PageAttr __attr) noexcept {
        if (__vma == nullptr || (__attr & bigos::mm::page_attr::PRESENT) == 0 ||
            (__attr & bigos::mm::page_attr::USER) == 0)
            return false;
        if ((__attr & bigos::mm::page_attr::WRITABLE) != 0 &&
            !permissions_include(__vma->permissions, bigos::proc::VmaPermission::Write))
            return false;
        if ((__attr & bigos::mm::page_attr::NO_EXECUTE) == 0 &&
            !permissions_include(__vma->permissions, bigos::proc::VmaPermission::Execute))
            return false;
        return true;
    }

    // Derives the leaf PTE attributes for an anonymous demand-zero page from the
    // owning VMA permissions. Writable pages stay non-executable; read-only and
    // non-executable pages keep NX so a stray fetch faults rather than executing.
    bigos::mm::PageAttr anonymous_page_attr(bigos::proc::VmaPermission __permissions) noexcept {
        bigos::mm::PageAttr attr = bigos::mm::page_attr::PRESENT | bigos::mm::page_attr::USER;
        if (permissions_include(__permissions, bigos::proc::VmaPermission::Write))
            attr |= bigos::mm::page_attr::WRITABLE;
        if (!permissions_include(__permissions, bigos::proc::VmaPermission::Execute))
            attr |= bigos::mm::page_attr::NO_EXECUTE;
        return attr;
    }

    // Derives the leaf PTE attributes for a file-backed page. The mapping is
    // always read-only (never WRITABLE), and non-executable unless the VMA
    // carries an explicit read-only-executable policy. A write to such a page
    // therefore always traps as a permission violation rather than splitting COW.
    bigos::mm::PageAttr file_backed_page_attr(bigos::proc::VmaPermission __permissions) noexcept {
        bigos::mm::PageAttr attr = bigos::mm::page_attr::PRESENT | bigos::mm::page_attr::USER;
        if (!permissions_include(__permissions, bigos::proc::VmaPermission::Execute))
            attr |= bigos::mm::page_attr::NO_EXECUTE;
        return attr;
    }

    struct SharedReadonlyKey {
        bigos::vfs::FileIdentity identity;
        uint64_t file_offset;
        uint32_t attrs;
    };

    struct SharedReadonlyPage {
        bool used;
        SharedReadonlyKey key;
        uint64_t frame_phys;
        uint32_t pte_refs;
        bigos::vfs::File *backing;
    };

    SharedReadonlyPage g_shared_readonly_pages[MAX_SHARED_READONLY_PAGES] = {};

    bool shared_readonly_key_equal(const SharedReadonlyKey &__a, const SharedReadonlyKey &__b) noexcept {
        return __a.identity.backend_id == __b.identity.backend_id && __a.identity.mount_id == __b.identity.mount_id &&
               __a.identity.object_id == __b.identity.object_id && __a.file_offset == __b.file_offset &&
               __a.attrs == __b.attrs;
    }

    bool shared_readonly_key_for_vma(const bigos::proc::VmaEntry *__vma, uint64_t __file_offset,
        bigos::mm::PageAttr __attr, SharedReadonlyKey *__out) noexcept {
        if (__vma == nullptr || __out == nullptr || __vma->file_backing == nullptr ||
            permissions_include(__vma->permissions, bigos::proc::VmaPermission::Write) ||
            (__attr & bigos::mm::page_attr::WRITABLE) != 0 || (__file_offset & (PAGE_SIZE - 1)) != 0)
            return false;
        bigos::vfs::FileIdentity identity = {};
        if (!bigos::vfs::file_identity(__vma->file_backing, &identity))
            return false;
        *__out = {identity, __file_offset, (uint32_t)(__attr & (bigos::mm::page_attr::USER |
                                                                 bigos::mm::page_attr::NO_EXECUTE))};
        return true;
    }

    SharedReadonlyPage *shared_readonly_find(const SharedReadonlyKey &__key) noexcept {
        for (uint32_t i = 0; i < MAX_SHARED_READONLY_PAGES; i++) {
            if (g_shared_readonly_pages[i].used && shared_readonly_key_equal(g_shared_readonly_pages[i].key, __key))
                return &g_shared_readonly_pages[i];
        }
        return nullptr;
    }

    SharedReadonlyPage *shared_readonly_free_slot() noexcept {
        for (uint32_t i = 0; i < MAX_SHARED_READONLY_PAGES; i++) {
            if (!g_shared_readonly_pages[i].used)
                return &g_shared_readonly_pages[i];
        }
        return nullptr;
    }

    bool shared_readonly_retain_mapping(SharedReadonlyPage *__entry, uint64_t *__phys) noexcept {
        if (__phys != nullptr)
            *__phys = bigos::mm::INVALID_PHYS_ADDR;
        if (__entry == nullptr || !__entry->used)
            return false;
        // Single-core SMP-preparation boundary: future SMP replaces this with a
        // shared-directory lock plus acquire/release ordering around pte_refs and
        // frame_ref updates. This path is only reached from blockable process
        // context, never from IRQ or scheduler critical sections.
        bigos::irq::InterruptGuard guard;
        if (!bigos::mm::frame_ref_inc(__entry->frame_phys))
            return false;
        __entry->pte_refs++;
        if (__phys != nullptr)
            *__phys = __entry->frame_phys;
        return true;
    }

    bool shared_readonly_retain_mapping_by_frame(uint64_t __phys) noexcept {
        for (uint32_t i = 0; i < MAX_SHARED_READONLY_PAGES; i++) {
            if (g_shared_readonly_pages[i].used && g_shared_readonly_pages[i].frame_phys == __phys) {
                uint64_t ignored = bigos::mm::INVALID_PHYS_ADDR;
                return shared_readonly_retain_mapping(&g_shared_readonly_pages[i], &ignored);
            }
        }
        return bigos::mm::frame_ref_inc(__phys);
    }

    void shared_readonly_drop_mapping(uint64_t __phys) noexcept {
        if (__phys == bigos::mm::INVALID_PHYS_ADDR)
            return;
        SharedReadonlyPage *entry = nullptr;
        {
            bigos::irq::InterruptGuard guard;
            for (uint32_t i = 0; i < MAX_SHARED_READONLY_PAGES; i++) {
                if (g_shared_readonly_pages[i].used && g_shared_readonly_pages[i].frame_phys == __phys) {
                    entry = &g_shared_readonly_pages[i];
                    break;
                }
            }
            if (entry != nullptr && entry->pte_refs > 0)
                entry->pte_refs--;
        }
        bigos::mm::frame_ref_dec_and_maybe_free(__phys);
        if (entry != nullptr && entry->pte_refs == 0) {
            bigos::vfs::File *backing = entry->backing;
            const uint64_t frame = entry->frame_phys;
            *entry = {};
            bigos::mm::frame_ref_dec_and_maybe_free(frame);
            if (backing != nullptr)
                bigos::vfs::release(backing);
        }
    }

    bool shared_readonly_publish_new(
        const SharedReadonlyKey &__key, bigos::vfs::File *__backing, uint64_t __phys, SharedReadonlyPage **__out) noexcept {
        if (__out != nullptr)
            *__out = nullptr;
        if (__backing == nullptr || __phys == 0)
            return false;
        bigos::irq::InterruptGuard guard;
        if (shared_readonly_find(__key) != nullptr)
            return false;
        SharedReadonlyPage *slot = shared_readonly_free_slot();
        if (slot == nullptr)
            return false;
        bigos::vfs::retain(__backing);
        *slot = {true, __key, __phys, 0, __backing};
        if (__out != nullptr)
            *__out = slot;
        return true;
    }

    void shared_readonly_cancel_unmapped(SharedReadonlyPage *__entry) noexcept {
        if (__entry == nullptr || !__entry->used || __entry->pte_refs != 0)
            return;
        bigos::vfs::File *backing = __entry->backing;
        const uint64_t frame = __entry->frame_phys;
        *__entry = {};
        bigos::mm::frame_ref_dec_and_maybe_free(frame);
        if (backing != nullptr)
            bigos::vfs::release(backing);
    }

    bool map_user_page_for_process(bigos::proc::Process *__process, uint64_t __vaddr, uint64_t __phys,
        bigos::mm::PageAttr __attr, bool __also_map_active_root) noexcept {
        if (__process == nullptr || !page_aligned(__vaddr) || !page_aligned(__phys))
            return false;
        const bigos::proc::VmaEntry *vma = internal_find_vma(&__process->vmas, __vaddr);
        if (!internal_vma_range_allowed(&__process->vmas, __vaddr, PAGE_SIZE, bigos::proc::VmaPermission::Read) ||
            !internal_vma_attr_allowed(vma, __attr) || vma == nullptr || !runtime_vma_allowed(__process, *vma))
            return false;
        if (!bigos::mm::map_page_in_root(__process->address_space_root, __vaddr, __phys, __attr))
            return false;
        if (__also_map_active_root && !bigos::arch::vm_user::is_active_address_space(__process->address_space_root) &&
            !bigos::mm::map_page(__vaddr, __phys, __attr))
            return false;
        return true;
    }

    void invalidate_process_tlb_page(bigos::proc::Process *__process, uint64_t __page) noexcept {
        bigos::mm::MmContext *context = ensure_mm_context(__process);
        if (context == nullptr)
            return;
        bigos::mm::TlbInvalidationRequest request = {
            bigos::mm::TlbInvalidationScope::Page,
            bigos::mm::TlbInvalidationReason::Generic,
            bigos::mm::mm_context_root(context),
            __page,
            PAGE_SIZE,
            0,
            true,
            context,
        };
        bigos::mm::invalidate_tlb(request);
    }

    void unmap_and_free_user_page(bigos::proc::Process *__process, uint64_t __vaddr) noexcept {
        if (__process == nullptr)
            return;
        uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
        if (bigos::mm::unmap_user_page_in_root(__process->address_space_root, __vaddr, &phys) &&
            phys != bigos::mm::INVALID_PHYS_ADDR) {
            invalidate_process_tlb_page(__process, __vaddr);
            bigos::mm::frame_ref_dec_and_maybe_free(phys);
        }
    }

    bool permissions_supported(bigos::proc::VmaPermission __permissions) noexcept {
        return (permissions_value(__permissions) &
                   ~(permissions_value(bigos::proc::VmaPermission::Read) |
                       permissions_value(bigos::proc::VmaPermission::Write) |
                       permissions_value(bigos::proc::VmaPermission::Execute))) == 0 &&
               !permissions_wx(__permissions);
    }

    bool lifecycle_range_bounds(uint64_t __addr, uint64_t __len, uint64_t *__end) noexcept {
        if (__end != nullptr)
            *__end = 0;
        uint64_t end = 0;
        if (__len == 0 || !page_aligned(__addr) || !page_aligned(__len) || add_overflow(__addr, __len, &end) ||
            end > bigos::proc::USER_LOW_HALF_LIMIT || end <= __addr)
            return false;
        if (__end != nullptr)
            *__end = end;
        return true;
    }

    bool anonymous_lifecycle_vma_compatible(const bigos::proc::Process *__process,
        const bigos::proc::VmaEntry *__entry, bigos::proc::VmaPermission __permissions) noexcept {
        return __entry != nullptr && __entry->backing == bigos::proc::VmaBacking::Anonymous &&
               __entry->purpose == bigos::proc::VmaPurpose::Anonymous &&
               __entry->growth == bigos::proc::VmaGrowth::None && runtime_vma_allowed(__process, *__entry) &&
               permissions_supported(__permissions);
    }

    bigos::proc::VmaEntry clipped_vma(
        const bigos::proc::VmaEntry &__entry, uint64_t __start, uint64_t __end) noexcept {
        bigos::proc::VmaEntry clipped = __entry;
        clipped.start = __start;
        clipped.end = __end;
        if (clipped.materialized_start < __start)
            clipped.materialized_start = __start;
        if (clipped.materialized_start > __end)
            clipped.materialized_start = __end;
        if (clipped.materialized_end < __start)
            clipped.materialized_end = __start;
        if (clipped.materialized_end > __end)
            clipped.materialized_end = __end;
        if (clipped.materialized_end < clipped.materialized_start)
            clipped.materialized_end = clipped.materialized_start;
        return clipped;
    }

    bool stage_anonymous_unmap(bigos::proc::Process *__process, uint64_t __addr, uint64_t __end,
        bigos::proc::VmaCollection *__staged) noexcept {
        if (__process == nullptr || __staged == nullptr)
            return false;
        *__staged = __process->vmas;
        uint64_t cursor = __addr;
        while (cursor < __end) {
            const bigos::proc::VmaEntry *entry = internal_find_vma(&__process->vmas, cursor);
            const bigos::proc::VmaPermission permissions =
                entry != nullptr ? entry->permissions : bigos::proc::VmaPermission::None;
            if (!anonymous_lifecycle_vma_compatible(__process, entry, permissions))
                return false;
            const uint64_t cut_end = entry->end < __end ? entry->end : __end;
            if (!internal_remove_vma(__staged, entry->start, entry->end))
                return false;
            if (entry->start < cursor && !internal_add_vma(__staged, clipped_vma(*entry, entry->start, cursor)))
                return false;
            if (cut_end < entry->end && !internal_add_vma(__staged, clipped_vma(*entry, cut_end, entry->end)))
                return false;
            cursor = cut_end;
        }
        return true;
    }

    bool stage_anonymous_protect(bigos::proc::Process *__process, uint64_t __addr, uint64_t __end,
        bigos::proc::VmaPermission __permissions, bigos::proc::VmaCollection *__staged) noexcept {
        if (__process == nullptr || __staged == nullptr || !permissions_supported(__permissions))
            return false;
        *__staged = __process->vmas;
        uint64_t cursor = __addr;
        while (cursor < __end) {
            const bigos::proc::VmaEntry *entry = internal_find_vma(&__process->vmas, cursor);
            if (!anonymous_lifecycle_vma_compatible(__process, entry, __permissions))
                return false;
            const uint64_t cut_end = entry->end < __end ? entry->end : __end;
            if (!internal_remove_vma(__staged, entry->start, entry->end))
                return false;
            if (entry->start < cursor && !internal_add_vma(__staged, clipped_vma(*entry, entry->start, cursor)))
                return false;
            bigos::proc::VmaEntry changed = clipped_vma(*entry, cursor, cut_end);
            changed.permissions = __permissions;
            if (!internal_add_vma(__staged, changed))
                return false;
            if (cut_end < entry->end && !internal_add_vma(__staged, clipped_vma(*entry, cut_end, entry->end)))
                return false;
            cursor = cut_end;
        }
        return true;
    }

#ifdef BIGOS_DYNAMIC_LINK
    // Stages a permission change over DynLib anonymous VMAs (the shared-object
    // mapping region). Same clip-and-replace shape as stage_anonymous_protect but
    // keyed on VmaPurpose::DynLib so the static lifecycle path is untouched.
    bool stage_dyn_lib_protect(bigos::proc::Process *__process, uint64_t __addr, uint64_t __end,
        bigos::proc::VmaPermission __permissions, bigos::proc::VmaCollection *__staged) noexcept {
        if (__process == nullptr || __staged == nullptr || !permissions_supported(__permissions))
            return false;
        *__staged = __process->vmas;
        uint64_t cursor = __addr;
        while (cursor < __end) {
            const bigos::proc::VmaEntry *entry = internal_find_vma(&__process->vmas, cursor);
            if (entry == nullptr || entry->purpose != bigos::proc::VmaPurpose::DynLib ||
                entry->backing != bigos::proc::VmaBacking::Anonymous)
                return false;
            const uint64_t cut_end = entry->end < __end ? entry->end : __end;
            if (!internal_remove_vma(__staged, entry->start, entry->end))
                return false;
            if (entry->start < cursor && !internal_add_vma(__staged, clipped_vma(*entry, entry->start, cursor)))
                return false;
            bigos::proc::VmaEntry changed = clipped_vma(*entry, cursor, cut_end);
            changed.permissions = __permissions;
            if (!internal_add_vma(__staged, changed))
                return false;
            if (cut_end < entry->end && !internal_add_vma(__staged, clipped_vma(*entry, cut_end, entry->end)))
                return false;
            cursor = cut_end;
        }
        return true;
    }
#endif   // BIGOS_DYNAMIC_LINK

    bool unmap_present_pages_in_range(bigos::proc::Process *__process, uint64_t __addr, uint64_t __end) noexcept {
        if (__process == nullptr)
            return false;
        for (uint64_t page = __addr; page < __end; page += PAGE_SIZE) {
            uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
            bigos::mm::PageAttr attr = 0;
            if (!bigos::mm::read_user_leaf_in_root(__process->address_space_root, page, &phys, &attr))
                continue;
            if ((attr & bigos::mm::page_attr::USER) == 0)
                return false;
            if (!bigos::mm::unmap_user_page_in_root(__process->address_space_root, page, &phys))
                return false;
            invalidate_process_tlb_page(__process, page);
            if (phys != bigos::mm::INVALID_PHYS_ADDR)
                bigos::mm::frame_ref_dec_and_maybe_free(phys);
        }
        return true;
    }

    bool unmap_shared_file_backed_pages(uint64_t __root, const bigos::proc::VmaCollection *__vmas) noexcept {
        if (__root == bigos::mm::INVALID_PHYS_ADDR || __vmas == nullptr)
            return true;
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &vma = __vmas->entries[i];
            if (!vma.used || vma.backing != bigos::proc::VmaBacking::FileBacked)
                continue;
            for (uint64_t page = vma.start; page < vma.end; page += PAGE_SIZE) {
                uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
                bigos::mm::PageAttr attr = 0;
                if (!bigos::mm::read_user_leaf_in_root(__root, page, &phys, &attr))
                    continue;
                if ((attr & bigos::mm::page_attr::USER) == 0)
                    return false;
                if (!bigos::mm::unmap_user_page_in_root(__root, page, &phys))
                    return false;
                shared_readonly_drop_mapping(phys);
            }
        }
        return true;
    }

    bool teardown_process_address_space(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr || __process->address_space_root == bigos::mm::INVALID_PHYS_ADDR)
            return true;
        if (__process->mm_context != nullptr)
            bigos::mm::mark_mm_context_dying(__process->mm_context);
        if (!unmap_shared_file_backed_pages(__process->address_space_root, &__process->vmas) ||
            !bigos::mm::teardown_user_address_space(__process->address_space_root))
            return false;
        forget_mm_context(__process);
        return true;
    }

    bool protect_present_pages_in_range(bigos::proc::Process *__process, uint64_t __addr, uint64_t __end,
        bigos::proc::VmaPermission __permissions) noexcept {
        if (__process == nullptr || !permissions_supported(__permissions))
            return false;
        const bigos::mm::PageAttr policy = anonymous_page_attr(__permissions);
        for (uint64_t page = __addr; page < __end; page += PAGE_SIZE) {
            uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
            bigos::mm::PageAttr attr = 0;
            if (!bigos::mm::read_user_leaf_in_root(__process->address_space_root, page, &phys, &attr))
                continue;
            bigos::mm::PageAttr new_attr = policy;
            if ((attr & bigos::mm::page_attr::PTE_COW) != 0 &&
                permissions_include(__permissions, bigos::proc::VmaPermission::Write))
                new_attr = (new_attr & ~bigos::mm::page_attr::WRITABLE) | bigos::mm::page_attr::PTE_COW;
            if (!bigos::mm::remap_user_page_in_root(__process->address_space_root, page, phys, new_attr))
                return false;
            invalidate_process_tlb_page(__process, page);
        }
        return true;
    }

    bigos::proc::Process *lookup_process(uint32_t __pid) noexcept {
        if (__pid == 0)
            return nullptr;
        for (bigos::proc::Process *process = g_process_list_head; process != nullptr; process = process->reg_next) {
            if (process->pid == __pid)
                return process;
        }
        return nullptr;
    }

    // Heap-backed Process object lifecycle. alloc_process_object returns a
    // zeroed object or nullptr on heap exhaustion (deterministic degradation, no
    // panic); free_process_object releases it. Static smoke objects are never
    // routed through these and keep heap_allocated == false.
    bigos::proc::Process *alloc_process_object() noexcept {
#ifdef BIGOS_GROWABLE_TABLES_SMOKE
        if (g_growable_force_alloc_fail)
            return nullptr;
#endif
        void *raw = bigos::kmalloc(sizeof(bigos::proc::Process));
        if (raw == nullptr)
            return nullptr;
        auto *process = (bigos::proc::Process *)raw;
        memset(process, 0, sizeof(*process));
        process->heap_allocated = true;
        return process;
    }

    void free_process_object(bigos::proc::Process *__process) noexcept {
        if (__process != nullptr && __process->heap_allocated)
            bigos::free(__process);
    }

    // Zeroes a Process for (re)use while preserving the heap_allocated ownership
    // flag, so create/fail paths can reset state without losing track of whether
    // the reaper must free the object's backing storage.
    void scrub_process(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        const bool heap_allocated = __process->heap_allocated;
        memset(__process, 0, sizeof(*__process));
        __process->heap_allocated = heap_allocated;
    }

    uint32_t alloc_pid() noexcept {
        // Bounded scan over the configured soft limit (plus headroom for the two
        // reserved values 0 / WAIT_ANY) so a large live process count never makes
        // PID allocation give up prematurely while still terminating.
        const uint32_t attempts = bigos::proc::MAX_PROCESSES_SOFT_LIMIT * 2 + 2;
        for (uint32_t attempt = 0; attempt < attempts; attempt++) {
            const uint32_t pid = g_next_pid++;
            if (g_next_pid == 0 || g_next_pid == bigos::proc::WAIT_ANY)
                g_next_pid = 1;
            if (pid != 0 && pid != bigos::proc::WAIT_ANY && lookup_process(pid) == nullptr)
                return pid;
        }
        return 0;
    }

    bool publish_process(bigos::proc::Process *__process, uint32_t __parent_pid) noexcept {
        if (__process == nullptr || __process->table_published)
            return false;
        if (g_process_count >= bigos::proc::MAX_PROCESSES_SOFT_LIMIT)
            return false;

        const uint32_t pid = alloc_pid();
        if (pid == 0)
            return false;

        __process->pid = pid;
        __process->parent_pid = __parent_pid;
        __process->first_child_pid = 0;
        __process->next_sibling_pid = 0;
        __process->next_reap_pid = 0;
        if (__process->pgid == 0)
            __process->pgid = pid;
        if (__process->sid == 0)
            __process->sid = pid;
        __process->table_published = true;
        __process->wait_status_consumed = false;
        __process->parent_waiting = false;

        // Push onto the head of the intrusive registry list (O(1), no search).
        __process->reg_prev = nullptr;
        __process->reg_next = g_process_list_head;
        if (g_process_list_head != nullptr)
            g_process_list_head->reg_prev = __process;
        g_process_list_head = __process;
        g_process_count++;

        bigos::proc::Process *parent = lookup_process(__parent_pid);
        if (parent != nullptr) {
            __process->next_sibling_pid = parent->first_child_pid;
            parent->first_child_pid = pid;
        }
        return true;
    }

    void unpublish_process(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr || !__process->table_published)
            return;

        bigos::proc::Process *parent = lookup_process(__process->parent_pid);
        if (parent != nullptr) {
            uint32_t *link = &parent->first_child_pid;
            while (*link != 0) {
                bigos::proc::Process *child = lookup_process(*link);
                if (child == nullptr)
                    break;
                if (child == __process) {
                    *link = child->next_sibling_pid;
                    break;
                }
                link = &child->next_sibling_pid;
            }
        }

        // Unlink from the intrusive registry list (O(1)).
        if (__process->reg_prev != nullptr)
            __process->reg_prev->reg_next = __process->reg_next;
        else
            g_process_list_head = __process->reg_next;
        if (__process->reg_next != nullptr)
            __process->reg_next->reg_prev = __process->reg_prev;
        __process->reg_next = nullptr;
        __process->reg_prev = nullptr;
        if (g_process_count > 0)
            g_process_count--;

        bigos::terminal::invalidate_foreground_pgid(__process->pgid);
        __process->table_published = false;
        __process->pid = 0;
        __process->parent_pid = 0;
        __process->first_child_pid = 0;
        __process->next_sibling_pid = 0;
        __process->next_reap_pid = 0;
        __process->pgid = 0;
        __process->sid = 0;
    }

    struct Elf64Header {
        uint8_t ident[16];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint64_t entry;
        uint64_t phoff;
        uint64_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    };

    struct Elf64ProgramHeader {
        uint32_t type;
        uint32_t flags;
        uint64_t offset;
        uint64_t vaddr;
        uint64_t paddr;
        uint64_t filesz;
        uint64_t memsz;
        uint64_t align;
    };

    struct LoadSegment {
        uint64_t vaddr;
        uint64_t memsz;
        uint64_t filesz;
        uint64_t offset;
        uint64_t map_base;
        uint64_t map_len;
        uint32_t flags;
        bigos::mm::PageAttr attr;
    };

    struct TrackedPage {
        uint64_t vaddr;
        bigos::mm::PageAttr attr;
    };

    bigos::mm::PageAttr segment_attr(uint32_t __flags) noexcept {
        bigos::mm::PageAttr attr = bigos::mm::page_attr::PRESENT | bigos::mm::page_attr::USER;
        if ((__flags & PF_W) != 0)
            attr |= bigos::mm::page_attr::WRITABLE | bigos::mm::page_attr::NO_EXECUTE;
        else if ((__flags & PF_X) == 0)
            attr |= bigos::mm::page_attr::NO_EXECUTE;
        return attr;
    }

    bool is_writable_executable(uint32_t __flags) noexcept {
        return (__flags & PF_W) != 0 && (__flags & PF_X) != 0;
    }

    bool elf_segment_shared_readonly_compatible(const LoadSegment &__segment, bigos::vfs::File *__backing) noexcept {
        return __backing != nullptr && (__segment.flags & PF_W) == 0 && __segment.filesz == __segment.memsz &&
               page_aligned(__segment.vaddr) && page_aligned(__segment.offset) && __segment.map_base == __segment.vaddr;
    }

    bool track_page(TrackedPage *__pages, uint32_t *__count, uint64_t __vaddr, bigos::mm::PageAttr __attr) noexcept {
        for (uint32_t i = 0; i < *__count; i++) {
            if (__pages[i].vaddr == __vaddr)
                return __pages[i].attr == __attr;
        }
        if (*__count >= ELF_MAX_TRACKED_PAGES)
            return false;
        __pages[*__count] = {__vaddr, __attr};
        (*__count)++;
        return true;
    }

    bigos::proc::UserElfLoadError validate_elf(const uint8_t *__image, uint64_t __image_len, LoadSegment *__segments,
        uint32_t *__segment_count, uint64_t *__entry) noexcept {
        if (__image == nullptr || __image_len < sizeof(Elf64Header) ||
            __image_len > bigos::proc::USER_ELF_MAX_FILE_BYTES)
            return bigos::proc::UserElfLoadError::InvalidArgument;

        const auto *ehdr = (const Elf64Header *)__image;
        if (*(const uint32_t *)ehdr->ident != ELF_MAGIC || ehdr->ident[4] != 2 || ehdr->ident[5] != 1 ||
            ehdr->ident[6] != ELF_VERSION_CURRENT)
            return bigos::proc::UserElfLoadError::UnsupportedElf;
        if (ehdr->type != ELF_TYPE_EXEC || ehdr->machine != ELF_MACHINE_X86_64 ||
            ehdr->version != ELF_VERSION_CURRENT || ehdr->ehsize != sizeof(Elf64Header) ||
            ehdr->phentsize != sizeof(Elf64ProgramHeader) || ehdr->phnum == 0 || ehdr->phnum > ELF_MAX_LOAD_SEGMENTS)
            return bigos::proc::UserElfLoadError::BadHeader;

        uint64_t ph_table_bytes = 0;
        uint64_t ph_table_end = 0;
        if (__builtin_mul_overflow((uint64_t)ehdr->phentsize, (uint64_t)ehdr->phnum, &ph_table_bytes) ||
            add_overflow(ehdr->phoff, ph_table_bytes, &ph_table_end) || ph_table_end > __image_len)
            return bigos::proc::UserElfLoadError::BadHeader;

        *__segment_count = 0;
        bool has_load = false;
        bool entry_executable = false;
        const uint64_t stack_base = bigos::proc::USER_STACK_TOP - bigos::proc::USER_STACK_PAGES * PAGE_SIZE;
        TrackedPage tracked_pages[ELF_MAX_TRACKED_PAGES] = {};
        uint32_t tracked_page_count = 0;

        for (uint16_t i = 0; i < ehdr->phnum; i++) {
            const uint64_t ph_offset = ehdr->phoff + (uint64_t)i * ehdr->phentsize;
            const auto *phdr = (const Elf64ProgramHeader *)(__image + ph_offset);
            if (phdr->type == PT_DYNAMIC || phdr->type == PT_INTERP || phdr->type == PT_TLS)
                return bigos::proc::UserElfLoadError::UnsupportedElf;
            if (phdr->type != PT_LOAD)
                return bigos::proc::UserElfLoadError::UnsupportedElf;
            if (phdr->memsz == 0 || phdr->filesz > phdr->memsz || is_writable_executable(phdr->flags))
                return bigos::proc::UserElfLoadError::UnsafePermissions;
            if (phdr->align != 0 && phdr->align != 1 && (!is_power_of_two(phdr->align) || phdr->align > PAGE_SIZE))
                return bigos::proc::UserElfLoadError::BadProgramHeader;

            uint64_t file_end = 0;
            uint64_t mem_end = 0;
            if (add_overflow(phdr->offset, phdr->filesz, &file_end) || file_end > __image_len ||
                add_overflow(phdr->vaddr, phdr->memsz, &mem_end) || !user_range_valid(phdr->vaddr, phdr->memsz) ||
                ranges_overlap(phdr->vaddr, phdr->memsz, stack_base, bigos::proc::USER_STACK_PAGES * PAGE_SIZE))
                return bigos::proc::UserElfLoadError::AddressOutOfRange;

            uint64_t map_end = 0;
            const uint64_t map_base = page_down(phdr->vaddr);
            if (!page_up(mem_end, &map_end) || map_end <= map_base)
                return bigos::proc::UserElfLoadError::AddressOutOfRange;

            for (uint32_t j = 0; j < *__segment_count; j++) {
                if (ranges_overlap(map_base, map_end - map_base, __segments[j].map_base, __segments[j].map_len))
                    return bigos::proc::UserElfLoadError::SegmentOverlap;
            }

            const bigos::mm::PageAttr attr = segment_attr(phdr->flags);
            for (uint64_t page = map_base; page < map_end; page += PAGE_SIZE) {
                if (!track_page(tracked_pages, &tracked_page_count, page, attr))
                    return bigos::proc::UserElfLoadError::UnsafePermissions;
            }

            LoadSegment &segment = __segments[*__segment_count];
            segment = {
                phdr->vaddr, phdr->memsz, phdr->filesz, phdr->offset, map_base, map_end - map_base, phdr->flags, attr};
            (*__segment_count)++;
            has_load = true;
            if ((phdr->flags & PF_X) != 0 && ehdr->entry >= phdr->vaddr && ehdr->entry < mem_end)
                entry_executable = true;
        }

        if (!has_load)
            return bigos::proc::UserElfLoadError::BadProgramHeader;
        if (!user_range_valid(ehdr->entry, 1))
            return bigos::proc::UserElfLoadError::AddressOutOfRange;
        if (!entry_executable)
            return bigos::proc::UserElfLoadError::EntryNotExecutable;
        *__entry = ehdr->entry;
        return bigos::proc::UserElfLoadError::Success;
    }

#ifdef BIGOS_DYNAMIC_LINK
    // GNU/OS program header types that appear in normal PIE/shared images. They
    // carry no loadable content for our bounded path and are skipped (never
    // mapped, never granting permissions). PT_TLS stays a hard reject.
    constexpr uint32_t PT_GNU_EH_FRAME = 0x6474e550;
    constexpr uint32_t PT_GNU_STACK = 0x6474e551;
    constexpr uint32_t PT_GNU_RELRO = 0x6474e552;
    constexpr uint32_t PT_GNU_PROPERTY = 0x6474e553;
    constexpr uint64_t DYN_INTERP_PATH_MAX = 64;

    // auxv a_type values for the initial-stack handshake (mirror bigos/elf.h).
    constexpr uint64_t AT_NULL = 0;
    constexpr uint64_t AT_PHDR = 3;
    constexpr uint64_t AT_PHENT = 4;
    constexpr uint64_t AT_PHNUM = 5;
    constexpr uint64_t AT_PAGESZ = 6;
    constexpr uint64_t AT_BASE = 7;
    constexpr uint64_t AT_ENTRY = 9;

    struct DynLoadInfo {
        uint64_t load_base;   // deterministic position-independent load base
        uint64_t entry;       // absolute entry = load_base + e_entry
        uint64_t phdr_addr;   // absolute program-header address (for AT_PHDR)
        uint16_t phnum;       // number of program headers
        uint16_t phentsize;   // size of one program header
    };

    // Validates a bounded position-independent ET_DYN image and produces load
    // segments with absolute (load_base + p_vaddr) addresses, so the existing
    // map_segment maps it unchanged. Mirrors validate_elf's bounds/overlap/W^X/
    // alignment/entry checks adjusted for the base offset. When __allow_interp is
    // true (main image) exactly-one PT_INTERP is accepted and its path is copied
    // out; the interpreter image itself MUST NOT contain a PT_INTERP. The static
    // ET_EXEC path is untouched.
    bigos::proc::UserElfLoadError validate_dyn_image(const uint8_t *__image, uint64_t __image_len, uint64_t __load_base,
        bool __allow_interp, LoadSegment *__segments, uint32_t *__segment_count, DynLoadInfo *__info,
        char *__interp_out, bool *__has_interp_out) noexcept {
        if (__image == nullptr || __segments == nullptr || __segment_count == nullptr || __info == nullptr ||
            __image_len < sizeof(Elf64Header) || __image_len > bigos::proc::USER_ELF_MAX_FILE_BYTES)
            return bigos::proc::UserElfLoadError::InvalidArgument;
        if (!page_aligned(__load_base))
            return bigos::proc::UserElfLoadError::InvalidArgument;

        const auto *ehdr = (const Elf64Header *)__image;
        if (*(const uint32_t *)ehdr->ident != ELF_MAGIC || ehdr->ident[4] != 2 || ehdr->ident[5] != 1 ||
            ehdr->ident[6] != ELF_VERSION_CURRENT)
            return bigos::proc::UserElfLoadError::UnsupportedElf;
        if (ehdr->type != ELF_TYPE_DYN || ehdr->machine != ELF_MACHINE_X86_64 ||
            ehdr->version != ELF_VERSION_CURRENT || ehdr->ehsize != sizeof(Elf64Header) ||
            ehdr->phentsize != sizeof(Elf64ProgramHeader) || ehdr->phnum == 0 || ehdr->phnum > ELF_MAX_LOAD_SEGMENTS)
            return bigos::proc::UserElfLoadError::BadHeader;

        uint64_t ph_table_bytes = 0;
        uint64_t ph_table_end = 0;
        if (__builtin_mul_overflow((uint64_t)ehdr->phentsize, (uint64_t)ehdr->phnum, &ph_table_bytes) ||
            add_overflow(ehdr->phoff, ph_table_bytes, &ph_table_end) || ph_table_end > __image_len)
            return bigos::proc::UserElfLoadError::BadHeader;

        *__segment_count = 0;
        bool has_load = false;
        bool entry_executable = false;
        bool has_interp = false;
        bool phdr_found = false;
        uint64_t phdr_vaddr = 0;
        const uint64_t stack_base = bigos::proc::USER_STACK_TOP - bigos::proc::USER_STACK_PAGES * PAGE_SIZE;
        TrackedPage tracked_pages[ELF_MAX_TRACKED_PAGES] = {};
        uint32_t tracked_page_count = 0;
        const uint64_t abs_entry = __load_base + ehdr->entry;

        for (uint16_t i = 0; i < ehdr->phnum; i++) {
            const uint64_t ph_offset = ehdr->phoff + (uint64_t)i * ehdr->phentsize;
            const auto *phdr = (const Elf64ProgramHeader *)(__image + ph_offset);
            if (phdr->type == PT_TLS)
                return bigos::proc::UserElfLoadError::UnsupportedElf;
            if (phdr->type == PT_INTERP) {
                if (!__allow_interp || has_interp)
                    return bigos::proc::UserElfLoadError::UnsupportedElf;
                uint64_t interp_end = 0;
                if (add_overflow(phdr->offset, phdr->filesz, &interp_end) || interp_end > __image_len ||
                    phdr->filesz == 0 || phdr->filesz > DYN_INTERP_PATH_MAX)
                    return bigos::proc::UserElfLoadError::BadProgramHeader;
                const char *src = (const char *)(__image + phdr->offset);
                if (src[phdr->filesz - 1] != 0)
                    return bigos::proc::UserElfLoadError::BadProgramHeader;
                if (__interp_out != nullptr)
                    memcpy(__interp_out, src, phdr->filesz);
                has_interp = true;
                continue;
            }
            if (phdr->type == PT_DYNAMIC || phdr->type == PT_PHDR || phdr->type == PT_NOTE ||
                phdr->type == PT_GNU_EH_FRAME || phdr->type == PT_GNU_STACK || phdr->type == PT_GNU_RELRO ||
                phdr->type == PT_GNU_PROPERTY)
                continue;
            if (phdr->type != PT_LOAD)
                continue;
            if (phdr->memsz == 0 || phdr->filesz > phdr->memsz || is_writable_executable(phdr->flags))
                return bigos::proc::UserElfLoadError::UnsafePermissions;
            if (phdr->align != 0 && phdr->align != 1 && (!is_power_of_two(phdr->align) || phdr->align > PAGE_SIZE))
                return bigos::proc::UserElfLoadError::BadProgramHeader;

            uint64_t abs_vaddr = 0;
            uint64_t file_end = 0;
            uint64_t mem_end = 0;
            if (add_overflow(__load_base, phdr->vaddr, &abs_vaddr) ||
                add_overflow(phdr->offset, phdr->filesz, &file_end) || file_end > __image_len ||
                add_overflow(abs_vaddr, phdr->memsz, &mem_end) || !user_range_valid(abs_vaddr, phdr->memsz) ||
                ranges_overlap(abs_vaddr, phdr->memsz, stack_base, bigos::proc::USER_STACK_PAGES * PAGE_SIZE))
                return bigos::proc::UserElfLoadError::AddressOutOfRange;

            uint64_t map_end = 0;
            const uint64_t map_base = page_down(abs_vaddr);
            if (!page_up(mem_end, &map_end) || map_end <= map_base)
                return bigos::proc::UserElfLoadError::AddressOutOfRange;

            for (uint32_t j = 0; j < *__segment_count; j++) {
                if (ranges_overlap(map_base, map_end - map_base, __segments[j].map_base, __segments[j].map_len))
                    return bigos::proc::UserElfLoadError::SegmentOverlap;
            }

            const bigos::mm::PageAttr attr = segment_attr(phdr->flags);
            for (uint64_t page = map_base; page < map_end; page += PAGE_SIZE) {
                if (!track_page(tracked_pages, &tracked_page_count, page, attr))
                    return bigos::proc::UserElfLoadError::UnsafePermissions;
            }

            // Locate the PT_LOAD covering the program-header file range (modern
            // Linux fs/binfmt_elf.c algorithm) so AT_PHDR points at mapped memory.
            if ((phdr->flags & PF_W) == 0 && phdr->offset <= ehdr->phoff &&
                ehdr->phoff + ph_table_bytes <= phdr->offset + phdr->filesz) {
                phdr_vaddr = phdr->vaddr + (ehdr->phoff - phdr->offset);
                phdr_found = true;
            }

            LoadSegment &segment = __segments[*__segment_count];
            segment = {
                abs_vaddr, phdr->memsz, phdr->filesz, phdr->offset, map_base, map_end - map_base, phdr->flags, attr};
            (*__segment_count)++;
            has_load = true;
            if ((phdr->flags & PF_X) != 0 && abs_entry >= abs_vaddr && abs_entry < mem_end)
                entry_executable = true;
        }

        if (!has_load)
            return bigos::proc::UserElfLoadError::BadProgramHeader;
        if (__allow_interp && !has_interp)
            return bigos::proc::UserElfLoadError::UnsupportedElf;
        // AT_PHDR coverage is only required for the main image (its interpreter
        // reads the phdrs from memory). The interpreter itself self-locates via
        // its _DYNAMIC (PC-relative) and never needs a mapped phdr table.
        if (__allow_interp && !phdr_found)
            return bigos::proc::UserElfLoadError::BadProgramHeader;
        if (!user_range_valid(abs_entry, 1))
            return bigos::proc::UserElfLoadError::AddressOutOfRange;
        if (!entry_executable)
            return bigos::proc::UserElfLoadError::EntryNotExecutable;

        __info->load_base = __load_base;
        __info->entry = abs_entry;
        __info->phdr_addr = __load_base + phdr_vaddr;
        __info->phnum = ehdr->phnum;
        __info->phentsize = ehdr->phentsize;
        if (__has_interp_out != nullptr)
            *__has_interp_out = has_interp;
        return bigos::proc::UserElfLoadError::Success;
    }

    // True when the image is a bounded ET_DYN main executable (dynamic-link path
    // candidate). Cheap header peek that leaves the static ET_EXEC path untouched.
    bool image_is_dyn_exec(const uint8_t *__image, uint64_t __image_len) noexcept {
        if (__image == nullptr || __image_len < sizeof(Elf64Header))
            return false;
        const auto *ehdr = (const Elf64Header *)__image;
        return *(const uint32_t *)ehdr->ident == ELF_MAGIC && ehdr->type == ELF_TYPE_DYN;
    }
#endif   // BIGOS_DYNAMIC_LINK

    bigos::proc::UserElfLoadError map_segment(bigos::proc::Process *__process, const uint8_t *__image,
        const LoadSegment &__segment, bool __shared_file_backed) noexcept {
        if (__shared_file_backed) {
            for (uint64_t page = __segment.map_base; page < __segment.map_base + __segment.map_len; page += PAGE_SIZE) {
                bigos::proc::VmaEntry *vma = internal_find_vma_mut(&__process->vmas, page);
                if (!bigos::proc::materialize_file_backed_page(__process, page, vma))
                    return bigos::proc::UserElfLoadError::MapFailed;
            }
            return bigos::proc::UserElfLoadError::Success;
        }
        for (uint64_t page = __segment.map_base; page < __segment.map_base + __segment.map_len; page += PAGE_SIZE) {
            const uint64_t phys = alloc_user_frame();
            bool mapped = false;
            if (phys == 0)
                return bigos::proc::UserElfLoadError::OutOfMemory;
            void *direct = bigos::mm::phys_to_direct(phys);
            if (direct == nullptr) {
                free_user_frame(phys);
                return bigos::proc::UserElfLoadError::CopyFailed;
            }
            memset(direct, 0, PAGE_SIZE);

            const uint64_t page_end = page + PAGE_SIZE;
            const uint64_t file_start = __segment.vaddr;
            const uint64_t file_end = __segment.vaddr + __segment.filesz;
            if (page < file_end && page_end > file_start) {
                const uint64_t copy_vaddr = page > file_start ? page : file_start;
                const uint64_t copy_end = page_end < file_end ? page_end : file_end;
                const uint64_t page_offset = copy_vaddr - page;
                const uint64_t file_offset = __segment.offset + (copy_vaddr - __segment.vaddr);
                memcpy((uint8_t *)direct + page_offset, __image + file_offset, copy_end - copy_vaddr);
            }

            // ELF segment pages live only in the process's own root; it is
            // activated on ring3 entry and the image bytes were copied through the
            // direct map above. Mapping into the active root too would pollute /
            // collide with whatever root is current (e.g. the kernel root during
            // an execve from another process), so do not duplicate there.
            if (!map_user_page_for_process(__process, page, phys, __segment.attr, false))
                goto fail;
            mapped = true;
            (void)mapped;
            continue;

        fail:
            if (!mapped)
                free_user_frame(phys);
            return bigos::proc::UserElfLoadError::MapFailed;
        }
        return bigos::proc::UserElfLoadError::Success;
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
        if (process == nullptr || process->reap_pending)
            return;
        process->reap_pending = true;
        process->state = bigos::proc::ProcessState::ReapPending;
        process->next_reap_pid = g_reap_head_pid;
        g_reap_head_pid = process->pid;
    }

    // Minimal orphan reparenting to PID-1 init (decision 9). When a non-init
    // process exits, walk its child chain and move every child onto init:
    // rewrite parent_pid to init's pid and splice the child into init's
    // first_child_pid chain. For children already in a terminal/zombie state,
    // post SIGCHLD to init and wake the process wait queue so init's while(1)
    // wait can reap them. Pure pointer rewiring over the existing
    // parent/child/sibling fields: no allocation, no IO, no lock, cannot fail.
    // When init is gone (g_init_process == nullptr, an abnormal path) it is
    // skipped and the existing self-reap fallback in mark_zombie_or_reap_pending
    // applies.
    void reparent_children_to_init(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        bigos::proc::Process *init = g_init_process;
        if (init == nullptr || init == __process || init->pid == 0 || !init->table_published)
            return;

        uint32_t child_pid = __process->first_child_pid;
        bool woke_init = false;
        while (child_pid != 0) {
            bigos::proc::Process *child = lookup_process(child_pid);
            if (child == nullptr)
                break;
            const uint32_t next = child->next_sibling_pid;

            child->parent_pid = init->pid;
            child->next_sibling_pid = init->first_child_pid;
            init->first_child_pid = child->pid;

            if (child->state == bigos::proc::ProcessState::Zombie ||
                child->state == bigos::proc::ProcessState::Terminated ||
                child->state == bigos::proc::ProcessState::Faulted) {
                (void)bigos::signal::kill(init, bigos::signal::SIGCHLD);
                woke_init = true;
            }
            child_pid = next;
        }
        __process->first_child_pid = 0;
        if (woke_init)
            (void)bigos::sched::wake_all(&g_process_wait_queue);
    }

    void mark_zombie_or_reap_pending(bigos::proc::Process *process) noexcept {
        if (process == nullptr)
            return;
        bigos::proc::Process *parent = lookup_process(process->parent_pid);
        // Child entering zombie (normal exit or signal/fault termination): set
        // SIGCHLD pending on the parent. Default action is Ignore so this never
        // changes existing wait/reaper behavior, and it allocates nothing.
        if (parent != nullptr)
            (void)bigos::signal::kill(parent, bigos::signal::SIGCHLD);
        if (parent != nullptr && !process->wait_status_consumed) {
            process->state = bigos::proc::ProcessState::Zombie;
            (void)bigos::sched::wake_all(&g_process_wait_queue);
            return;
        }

        process->wait_status_consumed = true;
        mark_reap_pending(process);
    }

    bool clone_process_kernel_stack_mapping(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr || __process->kernel_stack_base == nullptr ||
            __process->address_space_root == bigos::mm::INVALID_PHYS_ADDR)
            return false;

        const uint64_t base = (uint64_t)__process->kernel_stack_base;
        for (uint32_t i = 0; i < PROCESS_KERNEL_STACK_PAGES; i++) {
            if (!bigos::mm::__detail::clone_kernel_page_mapping_in_root(
                    __process->address_space_root, base + (uint64_t)i * PAGE_SIZE))
                return false;
        }
        return true;
    }

    constexpr uint32_t FD_TABLE_INITIAL_CAPACITY = 16;

    // Resets the growable fd storage to the empty state. Storage is allocated
    // lazily on the first install; create paths memset the Process first so this
    // only documents/normalizes the initial fd_table == nullptr / capacity == 0.
    void init_fd_table(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        __process->fd_table = nullptr;
        __process->fd_capacity = 0;
    }

    void free_fd_table(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr || __process->fd_table == nullptr)
            return;
        bigos::free(__process->fd_table);
        __process->fd_table = nullptr;
        __process->fd_capacity = 0;
    }

    // Grows the fd storage so it can hold at least __min_capacity entries,
    // bounded by MAX_FDS_SOFT_LIMIT. Returns false on heap exhaustion or when the
    // soft limit is reached (deterministic EMFILE at the call site, no panic).
    bool grow_fd_table(bigos::proc::Process *__process, uint32_t __min_capacity) noexcept {
        if (__process == nullptr || __min_capacity > bigos::proc::MAX_FDS_SOFT_LIMIT)
            return false;
        if (__process->fd_capacity >= __min_capacity)
            return true;
#ifdef BIGOS_GROWABLE_TABLES_SMOKE
        if (g_growable_force_alloc_fail)
            return false;
#endif

        uint32_t new_capacity = __process->fd_capacity == 0 ? FD_TABLE_INITIAL_CAPACITY : __process->fd_capacity * 2;
        if (new_capacity < __min_capacity)
            new_capacity = __min_capacity;
        if (new_capacity > bigos::proc::MAX_FDS_SOFT_LIMIT)
            new_capacity = bigos::proc::MAX_FDS_SOFT_LIMIT;

        auto *entries = (bigos::proc::FdEntry *)bigos::kmalloc(sizeof(bigos::proc::FdEntry) * new_capacity);
        if (entries == nullptr)
            return false;
        for (uint32_t i = 0; i < new_capacity; i++) {
            if (i < __process->fd_capacity) {
                entries[i] = __process->fd_table[i];
            } else {
                entries[i].file = nullptr;
                entries[i].close_on_exec = false;
                entries[i].readable = false;
            }
        }
        if (__process->fd_table != nullptr)
            bigos::free(__process->fd_table);
        __process->fd_table = entries;
        __process->fd_capacity = new_capacity;
        return true;
    }

    // Installs a single shared RDWR terminal handle at the standard descriptors
    // fd 0/1/2 of __process. The three descriptors share one vfs::File whose
    // ref_count ends at 3 (one per descriptor), all with close_on_exec = false so
    // they survive exec. Returns false (fd table unchanged) on fd-table growth or
    // handle-allocation failure, or when any of fd 0/1/2 is already occupied.
    // Non-IRQ / allocation-permitted context only.
    bool install_standard_fds(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return false;
        if (!grow_fd_table(__process, 3))
            return false;
        for (uint32_t fd = 0; fd < 3; fd++) {
            if (__process->fd_table[fd].file != nullptr)
                return false;
        }
        bigos::vfs::File *file = bigos::terminal::create_tty_file();
        if (file == nullptr)
            return false;
        // create_tty_file returns ref_count == 1 (consumed by fd 0); fd 1 and fd 2
        // each retain, bringing the shared handle to ref_count == 3. Because all
        // three descriptors share one vfs::File (one open file description), a
        // later F_SETFL(O_NONBLOCK) on any of fd 0/1/2 (and any dup of them) is
        // visible through all of them, matching POSIX shared-OFD semantics.
        for (uint32_t fd = 0; fd < 3; fd++) {
            if (fd != 0)
                bigos::vfs::retain(file);
            __process->fd_table[fd].file = file;
            __process->fd_table[fd].close_on_exec = false;
            __process->fd_table[fd].readable = file->readable;
        }
        return true;
    }

    uint64_t bounded_strlen(const char *__value, uint64_t __limit) noexcept {
        if (__value == nullptr)
            return UINT64_MAX;
        uint64_t len = 0;
        while (len <= __limit) {
            if (__value[len] == 0)
                return len;
            len++;
        }
        return UINT64_MAX;
    }

    bool copy_exec_args_to_stack(uint64_t __stack_phys, uint64_t __stack_base, const bigos::proc::ExecArgs *__args,
        uint64_t *__initial_sp) noexcept {
        if (__initial_sp == nullptr)
            return false;
        uint8_t *direct = (uint8_t *)bigos::mm::phys_to_direct(__stack_phys);
        if (direct == nullptr)
            return false;

        const char *empty_argv[] = {nullptr};
        bigos::proc::ExecArgs empty_args = {empty_argv, 0, nullptr, 0};
        const bigos::proc::ExecArgs *args = __args == nullptr ? &empty_args : __args;
        if (args->argc > bigos::proc::EXEC_MAX_ARGC || args->envc > bigos::proc::EXEC_MAX_ENVC)
            return false;
        if ((args->argc != 0 && args->argv == nullptr) || (args->envc != 0 && args->envp == nullptr))
            return false;

        uint64_t sp = PAGE_SIZE;
        uint64_t argv_user[bigos::proc::EXEC_MAX_ARGC] = {};
        uint64_t envp_user[bigos::proc::EXEC_MAX_ENVC] = {};

        for (uint32_t i = 0; i < args->envc; i++) {
            const uint64_t len = bounded_strlen(args->envp[i], bigos::proc::EXEC_MAX_STRING_BYTES);
            if (len == UINT64_MAX || len + 1 > sp)
                return false;
            sp -= len + 1;
            memcpy(direct + sp, args->envp[i], len + 1);
            envp_user[i] = __stack_base + sp;
        }
        for (uint32_t i = 0; i < args->argc; i++) {
            const uint64_t len = bounded_strlen(args->argv[i], bigos::proc::EXEC_MAX_STRING_BYTES);
            if (len == UINT64_MAX || len + 1 > sp)
                return false;
            sp -= len + 1;
            memcpy(direct + sp, args->argv[i], len + 1);
            argv_user[i] = __stack_base + sp;
        }

        sp &= ~(USER_INITIAL_STACK_ALIGN - 1);
        const uint64_t envp_bytes = (uint64_t)(args->envc + 1) * sizeof(uint64_t);
        const uint64_t argv_bytes = (uint64_t)(args->argc + 1) * sizeof(uint64_t);
        const uint64_t frame_bytes = sizeof(uint64_t) + argv_bytes + envp_bytes;
        if (frame_bytes > sp)
            return false;

        sp -= envp_bytes;
        uint64_t *envp_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < args->envc; i++)
            envp_table[i] = envp_user[i];
        envp_table[args->envc] = 0;

        sp -= argv_bytes;
        uint64_t *argv_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < args->argc; i++)
            argv_table[i] = argv_user[i];
        argv_table[args->argc] = 0;

        sp -= sizeof(uint64_t);
        *(uint64_t *)(direct + sp) = args->argc;
        *__initial_sp = __stack_base + sp;
        return true;
    }

#ifdef BIGOS_DYNAMIC_LINK
    struct AuxvEntry {
        uint64_t type;
        uint64_t val;
    };

    // Builds the initial user stack for the dynamic load path: the existing
    // [argc][argv][NULL][envp][NULL] layout plus a bounded auxv array appended
    // after the envp NULL terminator and terminated by AT_NULL. Append-only:
    // a static crt0 reading only up to the envp NULL is unaffected. The argc
    // landing address is kept 16-aligned for the System V ABI entry contract.
    bool copy_exec_args_and_auxv_to_stack(uint64_t __stack_phys, uint64_t __stack_base,
        const bigos::proc::ExecArgs *__args, const AuxvEntry *__auxv, uint32_t __auxv_count,
        uint64_t *__initial_sp) noexcept {
        if (__initial_sp == nullptr || __auxv == nullptr || __auxv_count == 0 ||
            __auxv_count > bigos::proc::USER_DYN_MAX_AUXV)
            return false;
        uint8_t *direct = (uint8_t *)bigos::mm::phys_to_direct(__stack_phys);
        if (direct == nullptr)
            return false;

        const char *empty_argv[] = {nullptr};
        bigos::proc::ExecArgs empty_args = {empty_argv, 0, nullptr, 0};
        const bigos::proc::ExecArgs *args = __args == nullptr ? &empty_args : __args;
        if (args->argc > bigos::proc::EXEC_MAX_ARGC || args->envc > bigos::proc::EXEC_MAX_ENVC)
            return false;
        if ((args->argc != 0 && args->argv == nullptr) || (args->envc != 0 && args->envp == nullptr))
            return false;

        uint64_t sp = PAGE_SIZE;
        uint64_t argv_user[bigos::proc::EXEC_MAX_ARGC] = {};
        uint64_t envp_user[bigos::proc::EXEC_MAX_ENVC] = {};

        for (uint32_t i = 0; i < args->envc; i++) {
            const uint64_t len = bounded_strlen(args->envp[i], bigos::proc::EXEC_MAX_STRING_BYTES);
            if (len == UINT64_MAX || len + 1 > sp)
                return false;
            sp -= len + 1;
            memcpy(direct + sp, args->envp[i], len + 1);
            envp_user[i] = __stack_base + sp;
        }
        for (uint32_t i = 0; i < args->argc; i++) {
            const uint64_t len = bounded_strlen(args->argv[i], bigos::proc::EXEC_MAX_STRING_BYTES);
            if (len == UINT64_MAX || len + 1 > sp)
                return false;
            sp -= len + 1;
            memcpy(direct + sp, args->argv[i], len + 1);
            argv_user[i] = __stack_base + sp;
        }

        sp &= ~(USER_INITIAL_STACK_ALIGN - 1);
        const uint64_t auxv_bytes = (uint64_t)__auxv_count * 2 * sizeof(uint64_t);
        const uint64_t envp_bytes = (uint64_t)(args->envc + 1) * sizeof(uint64_t);
        const uint64_t argv_bytes = (uint64_t)(args->argc + 1) * sizeof(uint64_t);
        const uint64_t frame_bytes = sizeof(uint64_t) + argv_bytes + envp_bytes + auxv_bytes;
        if (frame_bytes > sp)
            return false;
        // Pad so the final argc address lands on a 16-byte boundary.
        if (((sp - frame_bytes) & (USER_INITIAL_STACK_ALIGN - 1)) != 0) {
            if (sp < sizeof(uint64_t))
                return false;
            sp -= sizeof(uint64_t);
        }

        sp -= auxv_bytes;
        uint64_t *auxv_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < __auxv_count; i++) {
            auxv_table[i * 2] = __auxv[i].type;
            auxv_table[i * 2 + 1] = __auxv[i].val;
        }

        sp -= envp_bytes;
        uint64_t *envp_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < args->envc; i++)
            envp_table[i] = envp_user[i];
        envp_table[args->envc] = 0;

        sp -= argv_bytes;
        uint64_t *argv_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < args->argc; i++)
            argv_table[i] = argv_user[i];
        argv_table[args->argc] = 0;

        sp -= sizeof(uint64_t);
        *(uint64_t *)(direct + sp) = args->argc;
        *__initial_sp = __stack_base + sp;
        return true;
    }
#endif   // BIGOS_DYNAMIC_LINK

    // Derives the read-only copy-on-write leaf attribute from a writable
    // anonymous page's current attribute: clear WRITABLE, set PTE_COW, keep
    // present/user/NX. The next write to either sharer faults into the COW split
    // branch of try_handle_user_page_fault.
    bigos::mm::PageAttr cow_shared_attr(bigos::mm::PageAttr __attr) noexcept {
        return (__attr & ~bigos::mm::page_attr::WRITABLE) | bigos::mm::page_attr::PTE_COW;
    }

    // Copies one already-materialized parent leaf page into the child root per
    // decision 9. Returns false on allocation/map failure (the caller rolls back
    // the whole fork). guard pages and absent pages are handled by the caller
    // (it only invokes this for present user leaves).
    bool clone_user_leaf(bigos::proc::Process *__parent, bigos::proc::Process *__child,
        const bigos::proc::VmaEntry &__vma, uint64_t __page, uint64_t __parent_phys,
        bigos::mm::PageAttr __parent_attr) noexcept {
        if (__vma.backing == bigos::proc::VmaBacking::ElfSegment) {
            // ELF segments (text / rodata / writable data) get an independent
            // child frame with the segment's own attributes; no COW, no sharing.
            const uint64_t child_phys = alloc_user_frame();
            if (child_phys == 0)
                return false;
            void *src = bigos::mm::phys_to_direct(__parent_phys);
            void *dst = bigos::mm::phys_to_direct(child_phys);
            if (src == nullptr || dst == nullptr) {
                free_user_frame(child_phys);
                return false;
            }
            memcpy(dst, src, PAGE_SIZE);
            if (!bigos::mm::map_page_in_root(__child->address_space_root, __page, child_phys, __parent_attr)) {
                free_user_frame(child_phys);
                return false;
            }
            return true;
        }

        if (__vma.backing == bigos::proc::VmaBacking::FileBacked) {
            // File-backed read-only page: share the underlying frame between
            // parent and child (it never becomes writable, so no COW marker is
            // needed), counting the extra owner. Decision 4: already-materialized
            // pages are shared, not deep-copied; unmaterialized pages re-fault
            // independently through the cache in whichever process touches them.
            if (!shared_readonly_retain_mapping_by_frame(__parent_phys))
                return false;
            if (!bigos::mm::map_page_in_root(__child->address_space_root, __page, __parent_phys, __parent_attr)) {
                shared_readonly_drop_mapping(__parent_phys);
                return false;
            }
            return true;
        }

        // Anonymous backing.
        if ((__parent_attr & bigos::mm::page_attr::WRITABLE) != 0) {
            // Writable anonymous page -> COW share: downgrade both sides to
            // read-only + PTE_COW over the same frame and bump the share count.
            const bigos::mm::PageAttr cow_attr = cow_shared_attr(__parent_attr);
            if (!bigos::mm::frame_ref_inc(__parent_phys))
                return false;
            if (!bigos::mm::map_page_in_root(__child->address_space_root, __page, __parent_phys, cow_attr)) {
                bigos::mm::frame_ref_dec_and_maybe_free(__parent_phys);
                return false;
            }
            if (!bigos::mm::remap_user_page_in_root(__parent->address_space_root, __page, __parent_phys, cow_attr)) {
                // Child mapping already counts the share; teardown via fork
                // rollback will decrement it. Leaving the parent writable here is
                // unsafe (it could mutate the now-shared frame), so fail hard.
                return false;
            }
            invalidate_process_tlb_page(__parent, __page);
            return true;
        }

        // Read-only anonymous page: share directly (it never becomes writable, so
        // no COW marker is needed), counting the extra owner.
        if (!bigos::mm::frame_ref_inc(__parent_phys))
            return false;
        if (!bigos::mm::map_page_in_root(__child->address_space_root, __page, __parent_phys, __parent_attr)) {
            bigos::mm::frame_ref_dec_and_maybe_free(__parent_phys);
            return false;
        }
        return true;
    }

    bool clone_user_address_space_cow(bigos::proc::Process *__parent, bigos::proc::Process *__child) noexcept {
        if (__parent == nullptr || __child == nullptr)
            return false;

        const uint64_t child_root = bigos::mm::derive_user_address_space_root();
        __child->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (child_root == bigos::mm::INVALID_PHYS_ADDR)
            return false;
        if (!attach_mm_context(__child, child_root)) {
            (void)bigos::mm::teardown_user_address_space(child_root);
            return false;
        }
        if (!clone_process_kernel_stack_mapping(__child)) {
            (void)teardown_process_address_space(__child);
            return false;
        }

        // Value-copy the VMA collection (ranges, purposes, backings, growth,
        // permissions, materialization accounting, heap/anon bookkeeping).
        __child->runtime_layout = __parent->runtime_layout;
        __child->vmas = __parent->vmas;

        // File-backed VMAs carry a retained backing-file reference. The value copy
        // duplicated the pointer, so the child must take its own retain per
        // file-backed VMA; teardown/exec release one per child VMA symmetrically.
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &vma = __child->vmas.entries[i];
            if (vma.used && vma.backing == bigos::proc::VmaBacking::FileBacked && vma.file_backing != nullptr)
                bigos::vfs::retain(vma.file_backing);
        }

        // Walk every VMA's registered range and replicate only the pages that are
        // actually present in the parent root. Unmaterialized lazy pages are left
        // absent in the child and re-fault later through the unified handler;
        // guard pages have no leaves and are skipped.
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &vma = __parent->vmas.entries[i];
            if (!vma.used || vma.backing == bigos::proc::VmaBacking::Guard)
                continue;
            for (uint64_t page = vma.start; page < vma.end; page += PAGE_SIZE) {
                uint64_t parent_phys = bigos::mm::INVALID_PHYS_ADDR;
                bigos::mm::PageAttr parent_attr = 0;
                if (!bigos::mm::read_user_leaf_in_root(__parent->address_space_root, page, &parent_phys, &parent_attr))
                    continue;   // not materialized: copy metadata only (already done)
                if (!clone_user_leaf(__parent, __child, vma, page, parent_phys, parent_attr))
                    return false;
            }
        }
        return true;
    }

    bool clone_fd_table(bigos::proc::Process *__parent, bigos::proc::Process *__child) noexcept {
        if (__parent == nullptr || __child == nullptr)
            return false;
        __child->fd_table = nullptr;
        __child->fd_capacity = 0;
        if (__parent->fd_table == nullptr || __parent->fd_capacity == 0)
            return true;
        if (__parent->fd_capacity > bigos::proc::MAX_FDS_SOFT_LIMIT)
            return false;

        auto *entries = (bigos::proc::FdEntry *)bigos::kmalloc(sizeof(bigos::proc::FdEntry) * __parent->fd_capacity);
        if (entries == nullptr)
            return false;
        for (uint32_t i = 0; i < __parent->fd_capacity; i++) {
            entries[i] = __parent->fd_table[i];
            // Share the underlying read-only vfs::File: a retain per duplicated
            // descriptor keeps the File alive until both processes close it.
            if (entries[i].file != nullptr)
                bigos::vfs::retain(entries[i].file);
        }
        __child->fd_table = entries;
        __child->fd_capacity = __parent->fd_capacity;
        return true;
    }

    // First-scheduling entry for a forked child kernel thread. Mirrors
    // run_user_process but resumes the saved parent frame (rax already 0) rather
    // than a fresh entry/stack. arg is the child Process*.
    void fork_child_entry(void *__arg) noexcept {
        auto *child = (bigos::proc::Process *)__arg;
        if (child == nullptr || !child->fork_entry_valid)
            halt_failed("BIGOS_FORK_CHILD_FAILED invalid-child\n");

        if (!g_user_mode_initialized) {
            bigos::arch::vm_user::init_user_entry();
            g_user_mode_initialized = true;
        }

        set_current_process_slot(child);
        child->state = bigos::proc::ProcessState::Running;
        // Bind this kernel thread to the child process so the scheduler restores
        // the correct ring3 context (current process / user CR3 / rsp0) whenever
        // this thread is switched back in.
        bigos::sched::set_current_user_process(child);
        activate_process_kernel_address_space(child);
        bigos::arch::vm_user::set_kernel_stack_top(child->kernel_stack_top);
        bigos::serial_puts("BIGOS_FORK_CHILD_ENTER\n");
        if (!activate_process_user_address_space(child))
            halt_failed("BIGOS_FORK_CHILD_FAILED mm-context\n");
        bigos::arch::vm_user::resume_user(&child->fork_entry_frame);
    }
}   // namespace

namespace bigos::proc {
    // Drops each file-backed VMA's retained backing-file reference for __process.
    // Defined later in this namespace; forward-declared so the fork rollback,
    // exec replacement, and reap paths can release references symmetrically.
    void release_file_backed_vmas(Process *__process) noexcept;
    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len,
        const ExecArgs *__args, bigos::vfs::File *__elf_file) noexcept;

#ifdef BIGOS_DYNAMIC_LINK
    UserElfLoadError create_dyn_user_process(
        Process *__process, const void *__image, uint64_t __image_len, const ExecArgs *__args) noexcept;
#endif

    void init() noexcept {
        if (g_proc_initialized)
            return;
        g_process_list_head = nullptr;
        g_process_count = 0;
        set_current_process_slot(nullptr);
        g_reap_head_pid = 0;
        g_next_pid = 1;
        g_kernel_address_space_root = bigos::arch::vm_user::active_address_space_root();
        bigos::sched::init_wait_queue(&g_process_wait_queue);
        g_proc_initialized = true;
        bigos::serial_puts("BIGOS_PROC_INIT\n");
    }

    [[noreturn]] void launch_init_failed(const char *__reason) noexcept {
        // Deterministic degradation (decision 5): emit a BIGOS_INIT_* marker with
        // the failure reason, then enter the unified panic path (PID-1 prototype).
        bigos::serial_puts("BIGOS_INIT_LOAD_FAILED ");
        bigos::serial_puts(__reason);
        bigos::serial_puts("\n");
        bigos::kpanic(bigos::PanicCode::Generic, "launch_init");
    }

    void launch_init(void *) noexcept {
        bigos::vfs::Status status = bigos::vfs::init();
        if (status != bigos::vfs::Status::Success)
            launch_init_failed(bigos::vfs::status_name(status));

        bigos::vfs::File *file = nullptr;
        status = bigos::vfs::open_absolute(INIT_ELF_PATH, bigos::vfs::OPEN_RDONLY, &file);
        if (status != bigos::vfs::Status::Success)
            launch_init_failed(bigos::vfs::status_name(status));

        const uint64_t file_size = file->vnode != nullptr ? file->vnode->size : 0;
        if (file_size == 0 || file_size > USER_ELF_MAX_FILE_BYTES) {
            bigos::vfs::release(file);
            launch_init_failed("file-size");
        }

        void *image = bigos::kmalloc((size_t)file_size);
        if (image == nullptr) {
            bigos::vfs::release(file);
            launch_init_failed("buffer");
        }

        size_t bytes_read = 0;
        status = bigos::vfs::read(file, image, (size_t)file_size, &bytes_read);
        if (status != bigos::vfs::Status::Success || bytes_read != file_size) {
            const char *reason = status == bigos::vfs::Status::Success ? "short-read" : bigos::vfs::status_name(status);
            bigos::vfs::release(file);
            bigos::free(image);
            launch_init_failed(reason);
        }

        const char *argv[] = {INIT_ELF_PATH};
        const ExecArgs args = {argv, 1, nullptr, 0};
        // Heap-allocated init process object (replaces the former static Process
        // singleton). PID-1 semantics: a process-object allocation failure is a
        // deterministic init-launch failure rather than a separate panic class;
        // it routes through the existing launch_init_failed boundary.
        Process *init_process = alloc_process_object();
        if (init_process == nullptr) {
            bigos::free(image);
            launch_init_failed("process-object");
        }
        const UserElfLoadError load_status = create_elf_user_process(init_process, image, file_size, &args, file);
        bigos::vfs::release(file);
        bigos::free(image);
        if (load_status != UserElfLoadError::Success) {
            free_process_object(init_process);
            launch_init_failed(user_elf_load_error_name(load_status));
        }

        g_init_process = init_process;
        bigos::serial_puts("BIGOS_INIT_ENTER\n");
        run_user_process(init_process);
    }

    bool create_first_user_process(Process *__process) noexcept {
        if (__process == nullptr || sizeof(FIRST_USER_CODE) > PAGE_SIZE)
            return false;

        if (!g_proc_initialized)
            init();
        scrub_process(__process);
        uint64_t code_phys = 0;
        uint64_t data_phys = 0;
        uint64_t stack_phys = 0;
        bool code_mapped = false;
        bool data_mapped = false;
        bool stack_mapped = false;
        const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;

        __process->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (__process->kernel_stack_base == nullptr)
            return false;
        {
            const uint64_t root = bigos::mm::derive_user_address_space_root();
            if (root == bigos::mm::INVALID_PHYS_ADDR)
                goto fail;
            if (!attach_mm_context(__process, root)) {
                (void)bigos::mm::teardown_user_address_space(root);
                goto fail;
            }
        }
        __process->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (!clone_process_kernel_stack_mapping(__process))
            goto fail;
        internal_init_vmas(&__process->vmas);
        if (!init_runtime_layout(__process, USER_CODE_BASE, USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE + PAGE_SIZE))
            goto fail;
        if (!internal_add_process_vma(__process,
                {USER_CODE_BASE, USER_CODE_BASE + PAGE_SIZE, USER_CODE_BASE, USER_CODE_BASE + PAGE_SIZE,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Execute)),
                    VmaPurpose::Code, VmaBacking::Anonymous, VmaGrowth::None, true}) ||
            !internal_add_process_vma(__process,
                {USER_DATA_BASE, USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE, USER_DATA_BASE + PAGE_SIZE,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Data, VmaBacking::Anonymous, VmaGrowth::None, true}) ||
            !internal_add_process_vma(__process,
                {USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE + PAGE_SIZE + USER_HEAP_MAX_PAGES * PAGE_SIZE,
                    USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE + PAGE_SIZE,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Heap, VmaBacking::Anonymous, VmaGrowth::Up, true}) ||
            !internal_add_process_vma(__process,
                {stack_base - (USER_STACK_GROWTH_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE,
                    stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, stack_base, stack_base, VmaPermission::None,
                    VmaPurpose::StackGuard, VmaBacking::Guard, VmaGrowth::None, true}) ||
            !internal_add_process_vma(__process,
                {stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, USER_STACK_TOP, stack_base, USER_STACK_TOP,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Stack, VmaBacking::Anonymous, VmaGrowth::Down, true}))
            goto fail;
        __process->vmas.heap_base = USER_DATA_BASE + PAGE_SIZE;
        __process->vmas.heap_break = __process->vmas.heap_base;
        __process->vmas.heap_limit = __process->vmas.heap_base + USER_HEAP_MAX_PAGES * PAGE_SIZE;

        code_phys = alloc_user_frame();
        data_phys = alloc_user_frame();
        stack_phys = alloc_user_frame();
        if (code_phys == 0 || data_phys == 0 || stack_phys == 0)
            goto fail;
        if (!copy_to_frame(code_phys, FIRST_USER_CODE, sizeof(FIRST_USER_CODE)) || !zero_frame(data_phys) ||
            !zero_frame(stack_phys))
            goto fail;

        if (!map_user_page_for_process(__process, USER_CODE_BASE, code_phys, bigos::mm::page_attr::USER_CODE, true))
            goto fail;
        code_mapped = true;
        if (!map_user_page_for_process(__process, USER_DATA_BASE, data_phys, bigos::mm::page_attr::USER_DATA, true))
            goto fail;
        data_mapped = true;
        if (!map_user_page_for_process(__process, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA, true))
            goto fail;
        stack_mapped = true;

        __process->code_phys = code_phys;
        __process->data_phys = data_phys;
        __process->stack_phys = stack_phys;
        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top = (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        __process->entry = USER_CODE_BASE;
        __process->code = {USER_CODE_BASE, PAGE_SIZE};
        __process->data = {USER_DATA_BASE, PAGE_SIZE};
        __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
        __process->initial_stack = USER_STACK_TOP;
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
        // Non-fork creation: default to root identity (no login source yet) and
        // record the creation-time wall clock.
        __process->uid = bigos::cred::ROOT_UID;
        __process->gid = bigos::cred::ROOT_UID;
        __process->euid = bigos::cred::ROOT_UID;
        __process->egid = bigos::cred::ROOT_UID;
        __process->pgid = 0;
        __process->sid = 0;
        __process->start_unix_time = bigos::time::current_unix_time();
        init_cwd(__process);
        bigos::signal::init_state(__process);
        init_fd_table(__process);
        if (!publish_process(__process, ROOT_PARENT_PID))
            goto fail;
        return true;

    fail:
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR) {
            if (__process->mm_context != nullptr)
                bigos::mm::mark_mm_context_dying(__process->mm_context);
            (void)unmap_shared_file_backed_pages(__process->address_space_root, &__process->vmas);
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        }
        if (!code_mapped)
            free_user_frame(code_phys);
        if (!data_mapped)
            free_user_frame(data_phys);
        if (!stack_mapped)
            free_user_frame(stack_phys);
        if (__process->table_published)
            unpublish_process(__process);
        free_fd_table(__process);
        forget_mm_context(__process);
        scrub_process(__process);
        return false;
    }

    const char *user_elf_load_error_name(UserElfLoadError __error) noexcept {
        switch (__error) {
            case UserElfLoadError::Success:
                return "success";
            case UserElfLoadError::InvalidArgument:
                return "invalid-argument";
            case UserElfLoadError::UnsupportedElf:
                return "unsupported-elf";
            case UserElfLoadError::BadHeader:
                return "bad-header";
            case UserElfLoadError::BadProgramHeader:
                return "bad-program-header";
            case UserElfLoadError::AddressOutOfRange:
                return "address-out-of-range";
            case UserElfLoadError::SegmentOverlap:
                return "segment-overlap";
            case UserElfLoadError::UnsafePermissions:
                return "unsafe-permissions";
            case UserElfLoadError::EntryNotExecutable:
                return "entry-not-executable";
            case UserElfLoadError::OutOfMemory:
                return "out-of-memory";
            case UserElfLoadError::MapFailed:
                return "map-failed";
            case UserElfLoadError::CopyFailed:
                return "copy-failed";
        }
        return "unknown";
    }

    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len,
        const ExecArgs *__args, bigos::vfs::File *__elf_file) noexcept {
        if (__process == nullptr)
            return UserElfLoadError::InvalidArgument;
        if (!g_proc_initialized)
            init();
#ifdef BIGOS_DYNAMIC_LINK
        // Default-off dynamic path: ET_DYN main images route to the bounded
        // dynamic loader; the static ET_EXEC path below is reached unchanged for
        // every non-ET_DYN image.
        if (image_is_dyn_exec((const uint8_t *)__image, __image_len))
            return create_dyn_user_process(__process, __image, __image_len, __args);
        (void)__elf_file;
#endif
        LoadSegment segments[ELF_MAX_LOAD_SEGMENTS] = {};
        uint32_t segment_count = 0;
        uint64_t entry = 0;
        uint64_t image_low = UINT64_MAX;
        uint64_t image_high = 0;
        uint64_t heap_base = 0;
        UserElfLoadError error = validate_elf((const uint8_t *)__image, __image_len, segments, &segment_count, &entry);
        if (error != UserElfLoadError::Success)
            return error;

        scrub_process(__process);
        __process->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (__process->kernel_stack_base == nullptr)
            return UserElfLoadError::OutOfMemory;
        {
            const uint64_t root = bigos::mm::derive_user_address_space_root();
            if (root == bigos::mm::INVALID_PHYS_ADDR) {
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
            if (!attach_mm_context(__process, root)) {
                (void)bigos::mm::teardown_user_address_space(root);
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
        }
        __process->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (!clone_process_kernel_stack_mapping(__process)) {
            error = UserElfLoadError::MapFailed;
            goto fail;
        }
        internal_init_vmas(&__process->vmas);
        for (uint32_t i = 0; i < segment_count; i++) {
            if (segments[i].map_base < image_low)
                image_low = segments[i].map_base;
            if (segments[i].map_base + segments[i].map_len > image_high)
                image_high = segments[i].map_base + segments[i].map_len;
        }
        if (!page_up(image_high, &heap_base) || !init_runtime_layout(__process, image_low, image_high, heap_base)) {
            error = UserElfLoadError::AddressOutOfRange;
            goto fail;
        }
        for (uint32_t i = 0; i < segment_count; i++) {
            const VmaPurpose purpose = (segments[i].flags & PF_X) != 0 ? VmaPurpose::Code : VmaPurpose::Data;
            const bool shared_file_backed = elf_segment_shared_readonly_compatible(segments[i], __elf_file);
            bigos::vfs::File *file_backing = nullptr;
            if (shared_file_backed) {
                bigos::vfs::retain(__elf_file);
                file_backing = __elf_file;
            }
            if (!internal_add_process_vma(
                    __process, {segments[i].map_base, segments[i].map_base + segments[i].map_len, segments[i].map_base,
                                   shared_file_backed ? segments[i].map_base : segments[i].map_base + segments[i].map_len,
                                   segment_permissions(segments[i].flags), purpose,
                                   shared_file_backed ? VmaBacking::FileBacked : VmaBacking::ElfSegment,
                                   VmaGrowth::None, true, file_backing, shared_file_backed ? segments[i].offset : 0})) {
                if (file_backing != nullptr)
                    bigos::vfs::release(file_backing);
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
        }
        __process->vmas.heap_base = heap_base;
        __process->vmas.heap_break = heap_base;
        __process->vmas.heap_limit = __process->runtime_layout.heap.base + __process->runtime_layout.heap.len;
        if (!internal_add_process_vma(__process,
                {heap_base, __process->vmas.heap_limit, heap_base, heap_base,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Heap, VmaBacking::Anonymous, VmaGrowth::Up, true})) {
            error = UserElfLoadError::MapFailed;
            goto fail;
        }

        for (uint32_t i = 0; i < segment_count; i++) {
            const bool shared_file_backed = elf_segment_shared_readonly_compatible(segments[i], __elf_file);
            error = map_segment(__process, (const uint8_t *)__image, segments[i], shared_file_backed);
            if (error != UserElfLoadError::Success)
                goto fail;
        }

        {
            const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
            const uint64_t stack_phys = alloc_user_frame();
            uint64_t initial_sp = 0;
            if (!internal_add_process_vma(__process,
                    {stack_base - (USER_STACK_GROWTH_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE,
                        stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, stack_base, stack_base, VmaPermission::None,
                        VmaPurpose::StackGuard, VmaBacking::Guard, VmaGrowth::None, true}) ||
                !internal_add_process_vma(__process,
                    {stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, USER_STACK_TOP, stack_base, USER_STACK_TOP,
                        (VmaPermission)(permissions_value(VmaPermission::Read) |
                                        permissions_value(VmaPermission::Write)),
                        VmaPurpose::Stack, VmaBacking::Anonymous, VmaGrowth::Down, true})) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            if (stack_phys == 0) {
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
            if (!zero_frame(stack_phys)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!copy_exec_args_to_stack(stack_phys, stack_base, __args, &initial_sp)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!map_user_page_for_process(__process, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA, false)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            __process->stack_phys = stack_phys;
            __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
            __process->initial_stack = initial_sp;
        }

        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top = (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        __process->entry = entry;
        __process->code = {segments[0].map_base, segments[0].map_len};
        __process->data = {0, 0};
        for (uint32_t i = 0; i < segment_count; i++) {
            if ((segments[i].flags & PF_X) != 0)
                __process->code = {segments[i].map_base, segments[i].map_len};
            if ((segments[i].flags & PF_W) != 0 && __process->data.len == 0)
                __process->data = {segments[i].map_base, segments[i].map_len};
        }
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
        // init and non-fork ELF creation default to root identity (no login
        // source yet) and record the creation-time wall clock. exec reuses this
        // routine into a throwaway `prepared` object and intentionally does NOT
        // copy these fields back, so exec preserves identity and start time.
        __process->uid = bigos::cred::ROOT_UID;
        __process->gid = bigos::cred::ROOT_UID;
        __process->euid = bigos::cred::ROOT_UID;
        __process->egid = bigos::cred::ROOT_UID;
        __process->pgid = 0;
        __process->sid = 0;
        __process->start_unix_time = bigos::time::current_unix_time();
        init_cwd(__process);
        bigos::signal::init_state(__process);
        init_fd_table(__process);
        if (!publish_process(
                __process, current_process_slot() != nullptr ? current_process_slot()->pid : ROOT_PARENT_PID)) {
            error = UserElfLoadError::OutOfMemory;
            goto fail;
        }
        return UserElfLoadError::Success;

    fail:
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR) {
            if (__process->mm_context != nullptr)
                bigos::mm::mark_mm_context_dying(__process->mm_context);
            (void)unmap_shared_file_backed_pages(__process->address_space_root, &__process->vmas);
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        }
        release_file_backed_vmas(__process);
        if (__process->table_published)
            unpublish_process(__process);
        free_fd_table(__process);
        forget_mm_context(__process);
        scrub_process(__process);
        return error;
    }

    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len,
        const ExecArgs *__args) noexcept {
        return create_elf_user_process(__process, __image, __image_len, __args, nullptr);
    }

    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len) noexcept {
        return create_elf_user_process(__process, __image, __image_len, nullptr, nullptr);
    }

#ifdef BIGOS_DYNAMIC_LINK
    // Bounded dynamic load path (default-off). Loads an ET_DYN main image at
    // USER_DYN_EXEC_BASE plus its PT_INTERP-named interpreter at USER_INTERP_BASE,
    // builds an auxv-bearing initial stack, and sets the interpreter entry as the
    // ring3 entry point. Relocation/symbol binding happen entirely in ld.so; the
    // kernel only loads + hands off. Runs in ordinary (non-IRQ) context only.
    UserElfLoadError create_dyn_user_process(Process *__process, const void *__image, uint64_t __image_len,
        const ExecArgs *__args) noexcept {
        if (__process == nullptr || __image == nullptr)
            return UserElfLoadError::InvalidArgument;
        if (!g_proc_initialized)
            init();

        // Validate the ET_DYN main image at its deterministic base and extract the
        // interpreter path.
        LoadSegment main_segments[ELF_MAX_LOAD_SEGMENTS] = {};
        uint32_t main_segment_count = 0;
        DynLoadInfo main_info = {};
        char interp_path[DYN_INTERP_PATH_MAX] = {};
        bool has_interp = false;
        UserElfLoadError error = validate_dyn_image((const uint8_t *)__image, __image_len, USER_DYN_EXEC_BASE, true,
            main_segments, &main_segment_count, &main_info, interp_path, &has_interp);
        if (error != UserElfLoadError::Success)
            return error;
        if (!has_interp || interp_path[0] != '/')
            return UserElfLoadError::UnsupportedElf;

        // Read the interpreter ELF through the read-only VFS into a transient
        // kernel buffer (freed before return). Runs in kernel CR3 context.
        bigos::vfs::File *interp_file = nullptr;
        bigos::vfs::Status status = bigos::vfs::open_absolute(interp_path, bigos::vfs::OPEN_RDONLY, &interp_file);
        if (status != bigos::vfs::Status::Success)
            return UserElfLoadError::UnsupportedElf;
        const uint64_t interp_size = interp_file->vnode != nullptr ? interp_file->vnode->size : 0;
        if (interp_size == 0 || interp_size > USER_ELF_MAX_FILE_BYTES) {
            bigos::vfs::release(interp_file);
            return UserElfLoadError::UnsupportedElf;
        }
        void *interp_image = bigos::kmalloc((size_t)interp_size);
        if (interp_image == nullptr) {
            bigos::vfs::release(interp_file);
            return UserElfLoadError::OutOfMemory;
        }
        size_t interp_read = 0;
        status = bigos::vfs::read(interp_file, interp_image, (size_t)interp_size, &interp_read);
        bigos::vfs::release(interp_file);
        if (status != bigos::vfs::Status::Success || interp_read != interp_size) {
            bigos::free(interp_image);
            return UserElfLoadError::UnsupportedElf;
        }

        LoadSegment interp_segments[ELF_MAX_LOAD_SEGMENTS] = {};
        uint32_t interp_segment_count = 0;
        DynLoadInfo interp_info = {};
        error = validate_dyn_image((const uint8_t *)interp_image, interp_size, USER_INTERP_BASE, false, interp_segments,
            &interp_segment_count, &interp_info, nullptr, nullptr);
        if (error != UserElfLoadError::Success) {
            bigos::free(interp_image);
            return error;
        }

        uint64_t image_low = UINT64_MAX;
        uint64_t image_high = 0;
        uint64_t heap_base = 0;

        scrub_process(__process);
        __process->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (__process->kernel_stack_base == nullptr) {
            bigos::free(interp_image);
            return UserElfLoadError::OutOfMemory;
        }
        {
            const uint64_t root = bigos::mm::derive_user_address_space_root();
            if (root == bigos::mm::INVALID_PHYS_ADDR) {
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
            if (!attach_mm_context(__process, root)) {
                (void)bigos::mm::teardown_user_address_space(root);
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
        }
        __process->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (!clone_process_kernel_stack_mapping(__process)) {
            error = UserElfLoadError::MapFailed;
            goto fail;
        }
        internal_init_vmas(&__process->vmas);
        for (uint32_t i = 0; i < main_segment_count; i++) {
            if (main_segments[i].map_base < image_low)
                image_low = main_segments[i].map_base;
            if (main_segments[i].map_base + main_segments[i].map_len > image_high)
                image_high = main_segments[i].map_base + main_segments[i].map_len;
        }
        if (!page_up(image_high, &heap_base) ||
            !init_runtime_layout(__process, image_low, image_high, heap_base, true)) {
            error = UserElfLoadError::AddressOutOfRange;
            goto fail;
        }
        // Main image segment VMAs (anonymous-copy ElfSegment backing).
        for (uint32_t i = 0; i < main_segment_count; i++) {
            const VmaPurpose purpose = (main_segments[i].flags & PF_X) != 0 ? VmaPurpose::Code : VmaPurpose::Data;
            if (!internal_add_process_vma(__process,
                    {main_segments[i].map_base, main_segments[i].map_base + main_segments[i].map_len,
                        main_segments[i].map_base, main_segments[i].map_base + main_segments[i].map_len,
                        segment_permissions(main_segments[i].flags), purpose, VmaBacking::ElfSegment, VmaGrowth::None,
                        true, nullptr, 0})) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
        }
        // Interpreter segment VMAs (DynInterp purpose, inside the interp region).
        for (uint32_t i = 0; i < interp_segment_count; i++) {
            if (!internal_add_process_vma(__process,
                    {interp_segments[i].map_base, interp_segments[i].map_base + interp_segments[i].map_len,
                        interp_segments[i].map_base, interp_segments[i].map_base + interp_segments[i].map_len,
                        segment_permissions(interp_segments[i].flags), VmaPurpose::DynInterp, VmaBacking::ElfSegment,
                        VmaGrowth::None, true, nullptr, 0})) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
        }
        __process->vmas.heap_base = heap_base;
        __process->vmas.heap_break = heap_base;
        __process->vmas.heap_limit = __process->runtime_layout.heap.base + __process->runtime_layout.heap.len;
        if (!internal_add_process_vma(__process,
                {heap_base, __process->vmas.heap_limit, heap_base, heap_base,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Heap, VmaBacking::Anonymous, VmaGrowth::Up, true})) {
            error = UserElfLoadError::MapFailed;
            goto fail;
        }

        for (uint32_t i = 0; i < main_segment_count; i++) {
            error = map_segment(__process, (const uint8_t *)__image, main_segments[i], false);
            if (error != UserElfLoadError::Success)
                goto fail;
        }
        for (uint32_t i = 0; i < interp_segment_count; i++) {
            error = map_segment(__process, (const uint8_t *)interp_image, interp_segments[i], false);
            if (error != UserElfLoadError::Success)
                goto fail;
        }

        {
            const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
            const uint64_t stack_phys = alloc_user_frame();
            uint64_t initial_sp = 0;
            AuxvEntry auxv[] = {
                {AT_PHDR, main_info.phdr_addr},
                {AT_PHENT, main_info.phentsize},
                {AT_PHNUM, main_info.phnum},
                {AT_ENTRY, main_info.entry},
                {AT_BASE, interp_info.load_base},
                {AT_PAGESZ, PAGE_SIZE},
                {AT_NULL, 0},
            };
            if (!internal_add_process_vma(__process,
                    {stack_base - (USER_STACK_GROWTH_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE,
                        stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, stack_base, stack_base, VmaPermission::None,
                        VmaPurpose::StackGuard, VmaBacking::Guard, VmaGrowth::None, true}) ||
                !internal_add_process_vma(__process,
                    {stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, USER_STACK_TOP, stack_base, USER_STACK_TOP,
                        (VmaPermission)(permissions_value(VmaPermission::Read) |
                                        permissions_value(VmaPermission::Write)),
                        VmaPurpose::Stack, VmaBacking::Anonymous, VmaGrowth::Down, true})) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            if (stack_phys == 0) {
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
            if (!zero_frame(stack_phys)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!copy_exec_args_and_auxv_to_stack(
                    stack_phys, stack_base, __args, auxv, sizeof(auxv) / sizeof(auxv[0]), &initial_sp)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!map_user_page_for_process(__process, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA, false)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            __process->stack_phys = stack_phys;
            __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
            __process->initial_stack = initial_sp;
        }

        bigos::free(interp_image);
        interp_image = nullptr;

        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top = (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        // Ring3 entry is the interpreter entry (decision 1), not the main image.
        __process->entry = interp_info.entry;
        __process->code = {main_segments[0].map_base, main_segments[0].map_len};
        __process->data = {0, 0};
        for (uint32_t i = 0; i < main_segment_count; i++) {
            if ((main_segments[i].flags & PF_X) != 0)
                __process->code = {main_segments[i].map_base, main_segments[i].map_len};
            if ((main_segments[i].flags & PF_W) != 0 && __process->data.len == 0)
                __process->data = {main_segments[i].map_base, main_segments[i].map_len};
        }
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
        __process->uid = bigos::cred::ROOT_UID;
        __process->gid = bigos::cred::ROOT_UID;
        __process->euid = bigos::cred::ROOT_UID;
        __process->egid = bigos::cred::ROOT_UID;
        __process->pgid = 0;
        __process->sid = 0;
        __process->start_unix_time = bigos::time::current_unix_time();
        init_cwd(__process);
        bigos::signal::init_state(__process);
        init_fd_table(__process);
        if (!publish_process(
                __process, current_process_slot() != nullptr ? current_process_slot()->pid : ROOT_PARENT_PID)) {
            error = UserElfLoadError::OutOfMemory;
            goto fail;
        }
        return UserElfLoadError::Success;

    fail:
        if (interp_image != nullptr)
            bigos::free(interp_image);
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR) {
            if (__process->mm_context != nullptr)
                bigos::mm::mark_mm_context_dying(__process->mm_context);
            (void)unmap_shared_file_backed_pages(__process->address_space_root, &__process->vmas);
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        }
        release_file_backed_vmas(__process);
        if (__process->table_published)
            unpublish_process(__process);
        free_fd_table(__process);
        forget_mm_context(__process);
        scrub_process(__process);
        return error;
    }
#endif   // BIGOS_DYNAMIC_LINK

    UserElfLoadError exec_current_from_elf_image(const void *__image, uint64_t __image_len, const ExecArgs *__args,
        bigos::vfs::File *__elf_file) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running)
            return UserElfLoadError::InvalidArgument;
        if (bigos::arch::vm_user::is_active_address_space(process->address_space_root))
            return UserElfLoadError::MapFailed;

        Process *prepared = alloc_process_object();
        if (prepared == nullptr)
            return UserElfLoadError::OutOfMemory;
        UserElfLoadError error = create_elf_user_process(prepared, __image, __image_len, __args, __elf_file);
        if (error != UserElfLoadError::Success) {
            free_process_object(prepared);
            return error;
        }

        unpublish_process(prepared);
        bigos::mm::MmContext *old_mm_context = process->mm_context;
        const uint64_t old_root = process->address_space_root;
        const UserRange old_code = process->code;
        const UserRange old_data = process->data;
        const UserRange old_stack = process->stack;
        const UserRuntimeLayout old_runtime_layout = process->runtime_layout;
        const VmaCollection old_vmas = process->vmas;
        const uint64_t old_code_phys = process->code_phys;
        const uint64_t old_data_phys = process->data_phys;
        const uint64_t old_stack_phys = process->stack_phys;

        process->address_space_root = prepared->address_space_root;
        process->mm_context = prepared->mm_context;
        process->kernel_address_space_root = g_kernel_address_space_root;
        process->entry = prepared->entry;
        process->code = prepared->code;
        process->data = prepared->data;
        process->stack = prepared->stack;
        process->runtime_layout = prepared->runtime_layout;
        process->vmas = prepared->vmas;
        process->initial_stack = prepared->initial_stack;
        process->code_phys = prepared->code_phys;
        process->data_phys = prepared->data_phys;
        process->stack_phys = prepared->stack_phys;

        if (prepared->kernel_stack_base != nullptr)
            bigos::free_pages(prepared->kernel_stack_base);
        prepared->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        prepared->mm_context = nullptr;
        prepared->kernel_stack_base = nullptr;
        free_process_object(prepared);

        if (old_mm_context != nullptr)
            bigos::mm::mark_mm_context_dying(old_mm_context);
        if (g_kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::arch::vm_user::activate_kernel_address_space(g_kernel_address_space_root);
        if (old_root != bigos::mm::INVALID_PHYS_ADDR) {
            const bool unmapped_old_shared = unmap_shared_file_backed_pages(old_root, &old_vmas);
            const bool tore_down_old_root =
                unmapped_old_shared ? bigos::mm::teardown_user_address_space(old_root) : false;
            if (!unmapped_old_shared || !tore_down_old_root) {
                process->exit_code = EXEC_FAILURE_STATUS;
                process->fault_reason = EXEC_FAILURE_STATUS;
                mark_zombie_or_reap_pending(process);
                if (old_mm_context != nullptr)
                    bigos::mm::release_mm_context(old_mm_context);
                return UserElfLoadError::MapFailed;
            }
        }
        if (old_mm_context != nullptr)
            bigos::mm::release_mm_context(old_mm_context);
        close_on_exec_fds(process);
        // exec replaces the image: user handler dispositions point at now-invalid
        // user addresses, so reset them to default. The blocked mask and pending
        // set are preserved (decision 10). prepared.sig_* are discarded.
        bigos::signal::reset_handlers_on_exec(process);
        (void)old_code;
        (void)old_data;
        (void)old_stack;
        (void)old_runtime_layout;
        // The old image's address space has been torn down; drop this process's
        // retained backing-file references for any file-backed VMA it held so exec
        // does not leak them. The shared read-only cache frames are reference
        // counted and reclaimed by the teardown above.
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const VmaEntry &entry = old_vmas.entries[i];
            if (entry.used && entry.backing == VmaBacking::FileBacked && entry.file_backing != nullptr)
                bigos::vfs::release(entry.file_backing);
        }
        (void)old_code_phys;
        (void)old_data_phys;
        (void)old_stack_phys;
        return UserElfLoadError::Success;
    }

    // Maps a read-only VFS open failure to a deterministic negative errno for the
    // SYS_EXECVE path. NotFound -> -ENOENT so the shell PATH search can fall
    // through to the next candidate; permission/directory failures -> -EACCES.
    int64_t execve_open_errno(bigos::vfs::Status __status) noexcept {
        switch (__status) {
            case bigos::vfs::Status::NotFound:
                return -bigos::ENOENT;
            case bigos::vfs::Status::AccessDenied:
                return -bigos::EACCES;
            case bigos::vfs::Status::NotRegularFile:
                return -bigos::EACCES;
            case bigos::vfs::Status::NoMemory:
                return -bigos::ENOMEM;
            default:
                return -bigos::ENOENT;
        }
    }

    // Maps an ELF load failure to a deterministic negative errno. A malformed or
    // unloadable image is -ENOEXEC; allocation/mapping exhaustion is -ENOMEM; an
    // argv/envp/stack overflow is -E2BIG.
    int64_t execve_load_errno(UserElfLoadError __error) noexcept {
        switch (__error) {
            case UserElfLoadError::OutOfMemory:
            case UserElfLoadError::MapFailed:
                return -bigos::ENOMEM;
            case UserElfLoadError::CopyFailed:
                return -bigos::E2BIG;
            case UserElfLoadError::InvalidArgument:
            case UserElfLoadError::UnsupportedElf:
            case UserElfLoadError::BadHeader:
            case UserElfLoadError::BadProgramHeader:
            case UserElfLoadError::AddressOutOfRange:
            case UserElfLoadError::SegmentOverlap:
            case UserElfLoadError::UnsafePermissions:
            case UserElfLoadError::EntryNotExecutable:
            case UserElfLoadError::Success:
                return -bigos::ENOEXEC;
        }
        return -bigos::ENOEXEC;
    }

    int64_t execve_current(const char *__path, const ExecArgs *__args) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr)
            return -bigos::EFAULT;
        // Synchronous block IO / allocation follows: refuse in non-blockable
        // context per the same guard the other fd/VFS syscalls use.
        if (!bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;

        if (!bigos::vfs::initialized()) {
            const bigos::vfs::Status init_status = bigos::vfs::init();
            if (init_status != bigos::vfs::Status::Success)
                return -bigos::EIO;
        }

        bigos::vfs::File *file = nullptr;
        bigos::vfs::Status status =
            bigos::vfs::open(__path, process->cwd, bigos::vfs::OPEN_RDONLY, 0, bigos::cred::ROOT_UID, 0, &file);
        if (status != bigos::vfs::Status::Success)
            return execve_open_errno(status);

        const uint64_t file_size = file->vnode != nullptr ? file->vnode->size : 0;
        if (file_size == 0 || file_size > USER_ELF_MAX_FILE_BYTES) {
            bigos::vfs::release(file);
            return -bigos::ENOEXEC;
        }

        // Keep exec staging in kernel static storage instead of kmalloc. Syscall
        // execution may still run under the caller's user CR3 until commit, and
        // freshly grown heap pages are not guaranteed to be mapped there.
        static uint8_t exec_image[USER_ELF_MAX_FILE_BYTES];
        void *image = exec_image;

        size_t bytes_read = 0;
        status = bigos::vfs::read(file, image, (size_t)file_size, &bytes_read);
        if (status != bigos::vfs::Status::Success || bytes_read != file_size) {
            bigos::vfs::release(file);
            return -bigos::EIO;
        }

        const uint64_t user_root = process->address_space_root;
        activate_process_kernel_address_space(process);
        const UserElfLoadError error = exec_current_from_elf_image(image, file_size, __args, file);
        bigos::vfs::release(file);
        if (error != UserElfLoadError::Success) {
            // exec_current_from_elf_image only tears down the old image on the rare
            // teardown-failure path, which also marks the process a zombie. In that
            // case the old image is gone, so exit the thread rather than returning
            // to a dead address space.
            if (process->state != ProcessState::Running) {
                set_current_process_slot(nullptr);
                bigos::sched::thread_exit();
            }
            (void)user_root;
            if (!activate_process_user_address_space(process))
                return -bigos::ENOMEM;
            return execve_load_errno(error);
        }

        // Success: the current process now owns the new image. Enter ring3 at the
        // new entry on the freshly prepared initial stack; this does not return.
        bigos::arch::vm_user::set_kernel_stack_top(process->kernel_stack_top);
        if (!activate_process_user_address_space(process))
            halt_failed("BIGOS_USER_EXEC_FAILED mm-context\n");
        bigos::serial_puts("BIGOS_USER_EXEC\n");
        bigos::arch::vm_user::enter_user(process->entry, process->initial_stack);
    }

    [[noreturn]] void run_user_process(Process *__process) noexcept {
        if (__process == nullptr || __process->state != ProcessState::Created)
            halt_failed("BIGOS_USER_LOAD_FAILED invalid-process\n");

        if (!g_user_mode_initialized) {
            bigos::arch::vm_user::init_user_entry();
            g_user_mode_initialized = true;
        }

        set_current_process_slot(__process);
        __process->state = ProcessState::Running;
        // Install the standard tty descriptors fd 0/1/2 for a freshly entered
        // top-level process. fork inherits them through clone_fd_table and exec
        // keeps the existing table (close_on_exec = false), so this is the single
        // fresh-entry chokepoint; it must not run for forked children (they resume
        // via fork_child_entry) or bounded fd-table smokes (they never reach here).
        if (!install_standard_fds(__process))
            halt_failed("BIGOS_USER_ENTER_FAILED stdio-fds\n");
        // Bind this kernel thread to the process so the scheduler restores the
        // correct ring3 context (current process / user CR3 / rsp0) on resume.
        bigos::sched::set_current_user_process(__process);
        __process->kernel_address_space_root = bigos::arch::vm_user::active_address_space_root();
        bigos::arch::vm_user::set_kernel_stack_top(__process->kernel_stack_top);
        bigos::serial_puts("BIGOS_USER_ENTER\n");
        const uint64_t stack =
            __process->initial_stack != 0 ? __process->initial_stack : __process->stack.base + __process->stack.len;
        if (!activate_process_user_address_space(__process))
            halt_failed("BIGOS_USER_ENTER_FAILED mm-context\n");
        bigos::arch::vm_user::enter_user(__process->entry, stack);
    }

    Process *current_process() noexcept {
        return (Process *)bigos::cpu::current_state().current_process;
    }

    const char *current_cwd() noexcept {
        const Process *process = current_process_slot();
        if (process == nullptr || process->cwd[0] == 0)
            return "/";
        return process->cwd;
    }

    bool current_cwd_deleted() noexcept {
        const Process *process = current_process_slot();
        return process != nullptr && process->cwd_deleted;
    }

    void mark_cwd_deleted(const char *__abs_path) noexcept {
        if (__abs_path == nullptr || __abs_path[0] != '/')
            return;
        for (Process *process = g_process_list_head; process != nullptr; process = process->reg_next) {
            if (process->cwd[0] != 0 && path_equal(process->cwd, __abs_path))
                process->cwd_deleted = true;
        }
    }

    bool init_cwd(Process *__process, const char *__cwd) noexcept {
        if (__process == nullptr || __cwd == nullptr)
            return false;
        size_t len = 0;
        while (len <= bigos::vfs::MAX_PATH_LEN && __cwd[len] != 0)
            len++;
        if (len == 0 || len > bigos::vfs::MAX_PATH_LEN || __cwd[0] != '/')
            return false;
        memcpy(__process->cwd, __cwd, len + 1);
        __process->cwd_deleted = false;
        return true;
    }

    int64_t chdir_current(const char *__path) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || __path == nullptr)
            return -bigos::EINVAL;
        if (!bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        if (!bigos::vfs::initialized()) {
            const bigos::vfs::Status init_status = bigos::vfs::init();
            if (init_status != bigos::vfs::Status::Success)
                return (int64_t)init_status;
        }
        if (process->cwd_deleted && __path[0] != '/' && !starts_with_dotdot_component(__path))
            return -bigos::ENOENT;

        char resolved[bigos::vfs::MAX_PATH_LEN + 1];
        bigos::vfs::Status status = bigos::vfs::resolve_path(__path, process->cwd, resolved, sizeof(resolved));
        if (status != bigos::vfs::Status::Success)
            return (int64_t)status;
        bigos::Metadata metadata = {};
        status = bigos::vfs::stat_absolute(resolved, &metadata);
        if (status != bigos::vfs::Status::Success)
            return (int64_t)status;
        if (metadata.type != BIGOS_METADATA_TYPE_DIRECTORY)
            return -bigos::ENOTDIR;
        memcpy(process->cwd, resolved, strlen(resolved) + 1);
        process->cwd_deleted = false;
        return 0;
    }

    int64_t getcwd_current(char *__dst, size_t __dst_len) noexcept {
        const Process *process = current_process_slot();
        if (process == nullptr || __dst == nullptr || __dst_len == 0)
            return -bigos::EINVAL;
        const char *cwd = process->cwd[0] != 0 ? process->cwd : "/";
        const size_t len = strlen(cwd) + 1;
        if (__dst_len < len)
            return -bigos::ERANGE;
        memcpy(__dst, cwd, len);
        return 0;
    }

    void restore_current_user_context(Process *__process) noexcept {
        // A blocking syscall (e.g. wait/read) may switch to another user kernel
        // thread that clobbers the global ring3 context: g_current_process, the
        // active user CR3, and the TSS rsp0. When the original caller resumes in
        // the syscall dispatcher it must re-establish its own context before the
        // iretq back to ring3, otherwise a later demand fault sees the wrong (or
        // null) current process and the wrong address space. Kernel mappings are
        // shared across every root, so reading the process object here is safe
        // regardless of the CR3 left by the thread that ran in between.
        if (__process == nullptr || __process->state != ProcessState::Running) {
            return;
        }
        set_current_process_slot(__process);
        bigos::arch::vm_user::set_kernel_stack_top(__process->kernel_stack_top);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            (void)activate_process_user_address_space(__process);
    }

    void prepare_context_switch_to(Process *__next_process) noexcept {
        if (__next_process != nullptr) {
            restore_current_user_context(__next_process);
            return;
        }

        Process *current = current_process_slot();
        activate_process_kernel_address_space(current);
        set_current_process_slot(nullptr);
    }

    Process *find_process(uint32_t __pid) noexcept {
        Process *process = lookup_process(__pid);
        if (process == nullptr)
            return nullptr;
        // Only published, non-reaped processes are valid signal targets.
        if (process->state == ProcessState::Reaped || process->state == ProcessState::Empty)
            return nullptr;
        return process;
    }

    void init_vmas(VmaCollection *__vmas) noexcept {
        internal_init_vmas(__vmas);
    }

    bool add_vma(VmaCollection *__vmas, const VmaEntry &__entry) noexcept {
        return internal_add_vma(__vmas, __entry);
    }

    bool remove_vma(VmaCollection *__vmas, uint64_t __start, uint64_t __end) noexcept {
        return internal_remove_vma(__vmas, __start, __end);
    }

    const VmaEntry *find_vma(const VmaCollection *__vmas, uint64_t __addr) noexcept {
        return internal_find_vma(__vmas, __addr);
    }

    bool vma_range_allowed(
        const VmaCollection *__vmas, uint64_t __addr, uint64_t __len, VmaPermission __perm) noexcept {
        return internal_vma_range_allowed(__vmas, __addr, __len, __perm);
    }

    bool vma_attr_allowed(const VmaEntry *__vma, bigos::mm::PageAttr __attr) noexcept {
        return internal_vma_attr_allowed(__vma, __attr);
    }

    bool validate_user_buffer(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_IO_MAX_LEN)
            return false;
        return runtime_range_allowed(process, __addr, __len, VmaPermission::Read) &&
               bigos::mm::user_range_mapped(process->address_space_root, __addr, __len);
    }

    bool validate_user_io_buffer(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_IO_MAX_LEN)
            return false;
        return runtime_range_allowed(process, __addr, __len, VmaPermission::Write) &&
               bigos::mm::__detail::user_range_writable(process->address_space_root, __addr, __len);
    }

    bool copy_current_user_buffer(uint64_t __addr, void *__dst, uint64_t __len) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_IO_MAX_LEN)
            return false;
        if (!runtime_range_allowed(process, __addr, __len, VmaPermission::Read))
            return false;
        return bigos::mm::__detail::copy_from_user_root(process->address_space_root, __addr, __dst, __len);
    }

    bool copy_to_current_user_buffer(uint64_t __addr, const void *__src, uint64_t __len) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_IO_MAX_LEN)
            return false;
        if (!runtime_range_allowed(process, __addr, __len, VmaPermission::Write))
            return false;
        return bigos::mm::__detail::copy_to_user_root(process->address_space_root, __addr, __src, __len);
    }

    int64_t brk_current(uint64_t __new_break) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        if (__new_break == 0)
            return (int64_t)process->vmas.heap_break;
        if (__new_break < process->vmas.heap_base || __new_break > process->vmas.heap_limit)
            return -bigos::EINVAL;

        VmaEntry *heap = internal_find_vma_mut(&process->vmas, process->vmas.heap_base);
        if (heap == nullptr || heap->purpose != VmaPurpose::Heap || !runtime_vma_allowed(process, *heap))
            return -bigos::EFAULT;

        const uint64_t old_break = process->vmas.heap_break;
        uint64_t old_page_end = 0;
        uint64_t new_page_end = 0;
        if (!page_up(old_break, &old_page_end) || !page_up(__new_break, &new_page_end))
            return -bigos::EINVAL;

        if (new_page_end > old_page_end) {
            // Lazy growth: register the newly covered range by advancing only the
            // heap break and VMA metadata. Covered pages are materialized on first
            // access through try_handle_user_page_fault. No eager allocation, so
            // failure rollback is metadata-only (handled below before publishing).
        } else if (new_page_end < old_page_end) {
            // Shrink: unmap only pages that were actually materialized, per the
            // heap VMA materialization high-water mark.
            const uint64_t unmap_top = heap->materialized_end < old_page_end ? heap->materialized_end : old_page_end;
            for (uint64_t page = new_page_end; page < unmap_top; page += PAGE_SIZE)
                unmap_and_free_user_page(process, page);
            if (heap->materialized_end > new_page_end)
                heap->materialized_end = new_page_end;
        }

        process->vmas.heap_break = __new_break;
        return (int64_t)process->vmas.heap_break;
    }

    int64_t map_anonymous_current(uint64_t __len, uint64_t __permissions, uint64_t __flags) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        if (__flags != 0 || __len == 0 || __len > USER_ANON_MAX_PAGES * PAGE_SIZE)
            return -bigos::EINVAL;
        uint64_t len = 0;
        if (!page_up(__len, &len))
            return -bigos::EINVAL;

        const auto permissions = (VmaPermission)(uint8_t)__permissions;
        if ((permissions_value(permissions) &
                ~(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write) |
                    permissions_value(VmaPermission::Execute))) != 0 ||
            permissions_wx(permissions))
            return -bigos::EINVAL;

        uint64_t base = process->vmas.anon_next;
        uint64_t end = 0;
        const uint64_t anon_limit = process->runtime_layout.anonymous.base + process->runtime_layout.anonymous.len;
        while (!add_overflow(base, len, &end) && end <= anon_limit) {
            bool overlaps = false;
            for (uint32_t i = 0; i < MAX_VMAS; i++) {
                const VmaEntry &entry = process->vmas.entries[i];
                if (entry.used && base < entry.end && entry.start < end)
                    overlaps = true;
            }
            if (!overlaps)
                break;
            base += PAGE_SIZE;
        }
        if (add_overflow(base, len, &end) || end > anon_limit)
            return -bigos::EINVAL;

        if (!internal_add_process_vma(process, {base, end, base, base, permissions, VmaPurpose::Anonymous,
                                                   VmaBacking::Anonymous, VmaGrowth::None, true}))
            return -bigos::EFAULT;

        // Lazy backing: the VMA range is registered but no physical frames are
        // allocated or mapped here. Covered pages are materialized on first
        // access through try_handle_user_page_fault with the requested
        // permissions (writable pages stay non-executable). materialized_end
        // starts at base and advances as pages are faulted in.
        process->vmas.anon_next = end;
        return (int64_t)base;
    }

    int64_t unmap_anonymous_current(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        uint64_t end = 0;
        if (!lifecycle_range_bounds(__addr, __len, &end))
            return -bigos::EINVAL;

        VmaCollection staged = {};
        if (!stage_anonymous_unmap(process, __addr, end, &staged))
            return -bigos::EINVAL;
        if (!unmap_present_pages_in_range(process, __addr, end))
            return -bigos::EFAULT;
        process->vmas = staged;
        return 0;
    }

    int64_t protect_anonymous_current(uint64_t __addr, uint64_t __len, uint64_t __permissions) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        uint64_t end = 0;
        if (!lifecycle_range_bounds(__addr, __len, &end))
            return -bigos::EINVAL;
        const auto permissions = (VmaPermission)(uint8_t)__permissions;
        if (!permissions_supported(permissions))
            return -bigos::EINVAL;

        VmaCollection staged = {};
        if (!stage_anonymous_protect(process, __addr, end, permissions, &staged))
            return -bigos::EINVAL;
        if (!protect_present_pages_in_range(process, __addr, end, permissions))
            return -bigos::EFAULT;
        process->vmas = staged;
        return 0;
    }

    int64_t map_file_current(uint64_t __fd, uint64_t __offset, uint64_t __len, uint64_t __permissions,
        uint64_t __flags) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        // Reserved flags; offset and length must be page-aligned and non-zero.
        if (__flags != 0 || __len == 0 || !page_aligned(__offset) || !page_aligned(__len) ||
            __len > USER_FILEMAP_MAX_PAGES * PAGE_SIZE)
            return -bigos::EINVAL;
        // Read-only, non-W+X permission set (must include Read, must not include Write).
        const auto permissions = (VmaPermission)(uint8_t)__permissions;
        if ((permissions_value(permissions) &
                ~(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Execute))) != 0 ||
            !permissions_include(permissions, VmaPermission::Read) ||
            permissions_include(permissions, VmaPermission::Write))
            return -bigos::EINVAL;
        // offset + len must not overflow the file-offset space.
        uint64_t file_end = 0;
        if (add_overflow(__offset, __len, &file_end))
            return -bigos::EINVAL;
        (void)file_end;

        // fd must reference a readable regular file (not a directory or pipe).
        bigos::vfs::File *file = file_for_fd_current((uint32_t)__fd);
        if (file == nullptr || !file->readable)
            return -bigos::EBADF;
        if (file->vnode == nullptr || file->vnode->is_directory)
            return -bigos::EACCES;

        // Find a non-overlapping page-aligned slot in the file-mapping window.
        uint64_t base = process->vmas.filemap_next;
        uint64_t end = 0;
        const uint64_t filemap_limit =
            process->runtime_layout.file_mapped.base + process->runtime_layout.file_mapped.len;
        while (!add_overflow(base, __len, &end) && end <= filemap_limit) {
            bool overlaps = false;
            for (uint32_t i = 0; i < MAX_VMAS; i++) {
                const VmaEntry &entry = process->vmas.entries[i];
                if (entry.used && base < entry.end && entry.start < end)
                    overlaps = true;
            }
            if (!overlaps)
                break;
            base += PAGE_SIZE;
        }
        if (add_overflow(base, __len, &end) || end > filemap_limit)
            return -bigos::ENOMEM;

        // Retain the backing file for the lifetime of the mapping; released by
        // teardown/exec when the file-backed VMA metadata is dropped.
        bigos::vfs::retain(file);
        VmaEntry entry = {base, end, base, base, permissions, VmaPurpose::FileMapped, VmaBacking::FileBacked,
            VmaGrowth::None, true, file, __offset};
        if (!internal_add_process_vma(process, entry)) {
            bigos::vfs::release(file);
            return -bigos::ENOMEM;
        }

        // Lazy backing: no frames are mapped here. Covered pages materialize on
        // first read access through try_handle_user_page_fault.
        process->vmas.filemap_next = end;
        return (int64_t)base;
    }

#ifdef BIGOS_DYNAMIC_LINK
    int64_t dyn_map_current(uint64_t __base, uint64_t __len, uint64_t __permissions) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        // Dynamic-layout-only: the static path can never reach a dyn_lib region.
        if (!process->runtime_layout.dynamic)
            return -bigos::EINVAL;
        uint64_t len = 0;
        if (__len == 0 || !page_aligned(__base) || !page_up(__len, &len))
            return -bigos::EINVAL;
        const auto permissions = (VmaPermission)(uint8_t)__permissions;
        if (!permissions_supported(permissions) || !permissions_include(permissions, VmaPermission::Read))
            return -bigos::EINVAL;

        uint64_t end = 0;
        const uint64_t lib_base = process->runtime_layout.dyn_lib.base;
        const uint64_t lib_limit = lib_base + process->runtime_layout.dyn_lib.len;
        if (add_overflow(__base, len, &end) || __base < lib_base || end > lib_limit)
            return -bigos::EINVAL;
        for (uint32_t i = 0; i < MAX_VMAS; i++) {
            const VmaEntry &entry = process->vmas.entries[i];
            if (entry.used && __base < entry.end && entry.start < end)
                return -bigos::EINVAL;
        }
        if (!internal_add_process_vma(process, {__base, end, __base, __base, permissions, VmaPurpose::DynLib,
                                                   VmaBacking::Anonymous, VmaGrowth::None, true}))
            return -bigos::EFAULT;
        // Lazy demand-zero: pages materialize on first access with the requested
        // permissions through try_handle_user_page_fault.
        return (int64_t)__base;
    }

    int64_t dyn_protect_current(uint64_t __base, uint64_t __len, uint64_t __permissions) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        if (!process->runtime_layout.dynamic)
            return -bigos::EINVAL;
        uint64_t end = 0;
        if (!lifecycle_range_bounds(__base, __len, &end))
            return -bigos::EINVAL;
        const auto permissions = (VmaPermission)(uint8_t)__permissions;
        if (!permissions_supported(permissions) || !permissions_include(permissions, VmaPermission::Read))
            return -bigos::EINVAL;
        // The whole range must be covered by DynLib anonymous VMAs.
        const uint64_t lib_base = process->runtime_layout.dyn_lib.base;
        const uint64_t lib_limit = lib_base + process->runtime_layout.dyn_lib.len;
        if (__base < lib_base || end > lib_limit)
            return -bigos::EINVAL;
        uint64_t cursor = __base;
        while (cursor < end) {
            const VmaEntry *entry = internal_find_vma(&process->vmas, cursor);
            if (entry == nullptr || entry->purpose != VmaPurpose::DynLib ||
                entry->backing != VmaBacking::Anonymous)
                return -bigos::EINVAL;
            cursor = entry->end < end ? entry->end : end;
        }

        VmaCollection staged = {};
        if (!stage_dyn_lib_protect(process, __base, end, permissions, &staged))
            return -bigos::EINVAL;
        if (!protect_present_pages_in_range(process, __base, end, permissions))
            return -bigos::EFAULT;
        process->vmas = staged;
        return 0;
    }
#endif   // BIGOS_DYNAMIC_LINK

    // Resolves a copy-on-write write fault for the current process on a present,
    // read-only, PTE_COW page covered by a writable anonymous VMA. Defined after
    // try_handle_user_page_fault; declared here so the fault handler can call it.
    bool cow_split_current(Process *__process, uint64_t __page, const VmaEntry *__vma) noexcept;

    // Materializes a single not-present read fault on a read-only file-backed VMA
    // through the page/buffer cache. Defined after try_handle_user_page_fault;
    // declared here so the fault handler can call it. Returns false (deterministic
    // kill via the caller) on any failure without publishing a partial mapping.
    bool materialize_file_backed_page(Process *__process, uint64_t __page, VmaEntry *__vma) noexcept;

    bool try_handle_user_page_fault(uint64_t __fault_address, uint64_t __error_code) noexcept {
        Process *process = current_process_slot();
        // Allocation-safe, running-process precondition. A real ring3 #PF reaches
        // here under the nonblocking guard with IF=0, so can_block() would always
        // be false; the materialization/COW path only allocates (never blocks),
        // so it uses can_allocate_in_fault() instead. Non-running processes and
        // idle/critical contexts still fall through to the deterministic CPL3 kill
        // path in the caller.
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_allocate_in_fault())
            return false;
        const uint64_t page = page_down(__fault_address);
        // present bit set => either a copy-on-write write split or a genuine
        // protection violation. Check the COW case first; anything else is a kill.
        if ((__error_code & 0x1) != 0) {
            // Only a write (bit1) to a present, read-only, COW-marked page covered
            // by a writable anonymous VMA is a split candidate.
            if ((__error_code & 0x2) == 0)
                return false;
            VmaEntry *cow_vma = internal_find_vma_mut(&process->vmas, page);
            if (cow_vma == nullptr || cow_vma->backing != VmaBacking::Anonymous ||
                !runtime_vma_allowed(process, *cow_vma) ||
                !permissions_include(cow_vma->permissions, VmaPermission::Write))
                return false;
            uint64_t cow_phys = bigos::mm::INVALID_PHYS_ADDR;
            bigos::mm::PageAttr cow_attr = 0;
            if (!bigos::mm::read_user_leaf_in_root(process->address_space_root, page, &cow_phys, &cow_attr))
                return false;
            if ((cow_attr & bigos::mm::page_attr::PTE_COW) == 0 || (cow_attr & bigos::mm::page_attr::WRITABLE) != 0)
                return false;
            return cow_split_current(process, page, cow_vma);
        }

        VmaEntry *vma = internal_find_vma_mut(&process->vmas, page);
        if (vma == nullptr || !runtime_vma_allowed(process, *vma))
            return false;

        // Read-only file-backed not-present fault: a controlled exception to the
        // "non-anonymous backing has no recovery policy" kill rule. Only a read
        // (no write bit) is recoverable; a write to a read-only file-backed page,
        // or an instruction fetch into a non-executable file page, stays a kill.
        if (vma->backing == VmaBacking::FileBacked) {
            if ((__error_code & 0x2) != 0)
                return false;   // write to read-only file page -> permission kill
            if ((__error_code & (1ull << 4)) != 0 && !permissions_include(vma->permissions, VmaPermission::Execute))
                return false;   // fetch from NX file page -> kill
            if (page < vma->start || page >= vma->end)
                return false;   // out of range -> kill
            return materialize_file_backed_page(process, page, vma);
        }

        // No anonymous-backed VMA (guard / ELF file segments have no demand-zero
        // policy): unrecoverable -> kill.
        if (vma->backing != VmaBacking::Anonymous)
            return false;

        // Access-permission gate. A write fault requires Write permission; an
        // instruction fetch (NX) into a non-executable page is illegal. Both are
        // unrecoverable -> kill (the page stays non-executable for writable VMAs).
        if ((__error_code & 0x2) != 0 && !permissions_include(vma->permissions, VmaPermission::Write))
            return false;
        if ((__error_code & (1ull << 4)) != 0 && !permissions_include(vma->permissions, VmaPermission::Execute))
            return false;

        // Range gate, per growth direction. Downward stacks materialize below the
        // current materialized start; upward heap is bounded by the committed
        // heap break; fixed anonymous mappings cover the whole VMA range.
        if (vma->growth == VmaGrowth::Down) {
            if (page < vma->start || page >= vma->materialized_start)
                return false;
        } else {
            uint64_t registered_end = vma->end;
            if (vma->purpose == VmaPurpose::Heap) {
                if (!page_up(process->vmas.heap_break, &registered_end))
                    return false;
            }
            if (page < vma->start || page >= registered_end)
                return false;
        }

        const uint64_t attr = anonymous_page_attr(vma->permissions);
        const uint64_t phys = alloc_user_frame();
        if (phys == 0)
            return false;
        if (!zero_frame(phys) || !map_user_page_for_process(process, page, phys, attr, false)) {
            free_user_frame(phys);
            return false;
        }

        // Advance the materialization accounting. Downward stacks lower
        // materialized_start; everything else keeps a materialized_end
        // high-water mark so shrink/teardown can unmap only real pages.
        if (vma->growth == VmaGrowth::Down) {
            vma->materialized_start = page;
        } else if (page + PAGE_SIZE > vma->materialized_end) {
            vma->materialized_end = page + PAGE_SIZE;
        }
        return true;
    }

    // When the shared frame still has other owners it allocates a private copy,
    // remaps the faulting page writable, and drops the original's reference; when
    // the current process is the lone owner it restores write permission in
    // place. Returns false (deterministic kill via the caller) on allocation
    // failure without corrupting the shared frame or leaving a partial/writable
    // mapping for the faulting page.
    bool cow_split_current(Process *__process, uint64_t __page, const VmaEntry *__vma) noexcept {
        uint64_t old_phys = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr old_attr = 0;
        if (!bigos::mm::read_user_leaf_in_root(__process->address_space_root, __page, &old_phys, &old_attr))
            return false;

        // The page's intended writable attribute (NX preserved for data pages).
        const bigos::mm::PageAttr writable_attr =
            (old_attr & ~bigos::mm::page_attr::PTE_COW) | bigos::mm::page_attr::WRITABLE;
        if (__vma == nullptr || !runtime_vma_allowed(__process, *__vma) ||
            !internal_vma_attr_allowed(__vma, writable_attr))
            return false;

        if (!bigos::mm::frame_ref_is_shared(old_phys)) {
            // Lone owner: restore write permission and clear the marker in place,
            // no copy and no allocation.
            if (!bigos::mm::remap_user_page_in_root(__process->address_space_root, __page, old_phys, writable_attr))
                return false;
            invalidate_process_tlb_page(__process, __page);
            return true;
        }

        const uint64_t new_phys = alloc_user_frame();
        if (new_phys == 0)
            return false;
        void *src = bigos::mm::phys_to_direct(old_phys);
        void *dst = bigos::mm::phys_to_direct(new_phys);
        if (src == nullptr || dst == nullptr) {
            free_user_frame(new_phys);
            return false;
        }
        memcpy(dst, src, PAGE_SIZE);
        if (!bigos::mm::remap_user_page_in_root(__process->address_space_root, __page, new_phys, writable_attr)) {
            free_user_frame(new_phys);
            return false;
        }
        invalidate_process_tlb_page(__process, __page);
        // The faulting process now owns new_phys (implicit count 1); drop its hold
        // on the original shared frame, which the other sharer keeps mapped.
        bigos::mm::frame_ref_dec_and_maybe_free(old_phys);
        return true;
    }

    bool materialize_file_backed_page(Process *__process, uint64_t __page, VmaEntry *__vma) noexcept {
        if (__process == nullptr || __vma == nullptr || __vma->backing != VmaBacking::FileBacked ||
            __vma->file_backing == nullptr)
            return false;
        // Cache load may issue synchronous block IO, so it is only legal from a
        // blocking-capable context. A real ring3 #PF runs under the nonblocking
        // guard, so this materialization deterministically fails (and the caller
        // kills the process) unless the fault is driven from a blockable context.
        if (!bigos::sched::can_block())
            return false;

        // file offset = VMA file start offset + (faulting page - VMA start).
        const uint64_t page_delta = __page - __vma->start;
        uint64_t file_offset = 0;
        if (add_overflow(__vma->file_offset, page_delta, &file_offset))
            return false;
        const uint64_t attr = file_backed_page_attr(__vma->permissions);
        SharedReadonlyKey key = {};
        if (!shared_readonly_key_for_vma(__vma, file_offset, attr, &key))
            return false;

        if (SharedReadonlyPage *found = shared_readonly_find(key)) {
            uint64_t shared_phys = bigos::mm::INVALID_PHYS_ADDR;
            if (!shared_readonly_retain_mapping(found, &shared_phys))
                return false;
            if (!map_user_page_for_process(__process, __page, shared_phys, attr, false)) {
                shared_readonly_drop_mapping(shared_phys);
                return false;
            }
            if (__page + PAGE_SIZE > __vma->materialized_end)
                __vma->materialized_end = __page + PAGE_SIZE;
            return true;
        }

        const uint64_t file_size = __vma->file_backing->vnode != nullptr ? __vma->file_backing->vnode->size : 0;
        // Bytes of real file content that fall inside this page; the remainder of
        // the page (file tail beyond file_size, still within the VMA) is zero-fill.
        uint64_t in_file = 0;
        if (file_offset < file_size) {
            const uint64_t remaining = file_size - file_offset;
            in_file = remaining < PAGE_SIZE ? remaining : PAGE_SIZE;
        }

        const uint64_t phys = alloc_user_frame();
        if (phys == 0)
            return false;
        void *direct = bigos::mm::phys_to_direct(phys);
        if (direct == nullptr) {
            free_user_frame(phys);
            return false;
        }
        // Zero the whole frame first so the beyond-file-length tail is zero-filled
        // deterministically; then fill the in-file portion from the cache-backed
        // read. A single page may span multiple cache blocks; pread aggregates
        // them and fails the whole materialization on any underlying block IO error.
        memset(direct, 0, PAGE_SIZE);
        if (in_file != 0) {
            size_t got = 0;
            const bigos::vfs::Status status =
                bigos::vfs::pread(__vma->file_backing, file_offset, direct, (size_t)in_file, &got);
            if (status != bigos::vfs::Status::Success || got != in_file) {
                free_user_frame(phys);
                return false;
            }
        }

        SharedReadonlyPage *published = nullptr;
        if (!shared_readonly_publish_new(key, __vma->file_backing, phys, &published)) {
            free_user_frame(phys);
            return false;
        }
        uint64_t pte_phys = bigos::mm::INVALID_PHYS_ADDR;
        if (!shared_readonly_retain_mapping(published, &pte_phys)) {
            shared_readonly_cancel_unmapped(published);
            return false;
        }
        if (!map_user_page_for_process(__process, __page, pte_phys, attr, false)) {
            shared_readonly_drop_mapping(pte_phys);
            return false;
        }
        if (__page + PAGE_SIZE > __vma->materialized_end)
            __vma->materialized_end = __page + PAGE_SIZE;
        return true;
    }

    int64_t fork_current(const bigos::irq::InterruptFrame *__parent_frame) noexcept {
        Process *parent = current_process_slot();
        if (parent == nullptr || parent->state != ProcessState::Running || __parent_frame == nullptr)
            return -bigos::EINVAL;
        if (!bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        // Deterministic process soft-limit failure (no overflow / panic).
        if (g_process_count >= bigos::proc::MAX_PROCESSES_SOFT_LIMIT)
            return -bigos::EAGAIN;

        Process *child = alloc_process_object();
        if (child == nullptr)
            return -bigos::ENOMEM;

        // Independent kernel stack for the child kernel thread.
        child->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (child->kernel_stack_base == nullptr) {
            free_process_object(child);
            return -bigos::ENOMEM;
        }
        child->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        child->kernel_stack_top = (uint64_t)child->kernel_stack_base + child->kernel_stack_len;
        child->address_space_root = bigos::mm::INVALID_PHYS_ADDR;

        // COW address-space copy. On failure roll back: any frames the partial
        // copy shared/allocated are owned by the child root and are released by
        // teardown (reference-count-aware), so a single teardown undoes them.
        if (!clone_user_address_space_cow(parent, child)) {
            // clone_user_address_space_cow may have retained child file-backed VMA
            // references before a later page-clone step failed; release them.
            (void)teardown_process_address_space(child);
            release_file_backed_vmas(child);
            bigos::free_pages(child->kernel_stack_base);
            free_process_object(child);
            return -bigos::ENOMEM;
        }

        if (!clone_fd_table(parent, child)) {
            (void)teardown_process_address_space(child);
            release_file_backed_vmas(child);
            bigos::free_pages(child->kernel_stack_base);
            free_process_object(child);
            return -bigos::ENOMEM;
        }

        // Mirror the parent's user-visible process metadata.
        child->entry = parent->entry;
        child->code = parent->code;
        child->data = parent->data;
        child->stack = parent->stack;
        child->initial_stack = parent->initial_stack;
        child->kernel_address_space_root = parent->kernel_address_space_root;
        child->state = ProcessState::Created;
        child->reap_pending = false;
        child->resources_reclaimed = false;
        child->exit_code = 0;
        child->fault_reason = 0;
        // Identity inheritance: the child copies the parent's identity quad
        // field-by-field (no allocation, no new failure point). The child is a
        // new process, so its start timestamp is the fork-time wall clock.
        child->uid = parent->uid;
        child->gid = parent->gid;
        child->euid = parent->euid;
        child->egid = parent->egid;
        child->pgid = parent->pgid;
        child->sid = parent->sid;
        child->start_unix_time = bigos::time::current_unix_time();
        memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
        child->cwd_deleted = parent->cwd_deleted;

        // Signal inheritance: the child inherits the disposition table and blocked
        // mask field-by-field; its pending set starts empty (POSIX fork). No new
        // allocation, no failure path.
        bigos::signal::inherit_on_fork(child, parent);

        // Child resumes from the parent's syscall return context with rax 0. The
        // architecture boundary owns the raw frame/tail interpretation.
        if (!bigos::arch::vm_user::capture_user_resume_frame(__parent_frame, 0, &child->fork_entry_frame)) {
            close_all_fds(child);
            free_fd_table(child);
            (void)teardown_process_address_space(child);
            release_file_backed_vmas(child);
            bigos::free_pages(child->kernel_stack_base);
            free_process_object(child);
            return -bigos::EINVAL;
        }
        child->fork_entry_valid = true;

        if (!publish_process(child, parent->pid)) {
            close_all_fds(child);
            free_fd_table(child);
            (void)teardown_process_address_space(child);
            release_file_backed_vmas(child);
            bigos::free_pages(child->kernel_stack_base);
            free_process_object(child);
            return -bigos::EAGAIN;
        }

        const uint32_t child_pid = child->pid;
        // Spawn the child's kernel thread; its first scheduling enters ring3 via
        // fork_child_entry. On failure unpublish + roll back fully.
        if (bigos::sched::create_kernel_thread(&fork_child_entry, child) == bigos::sched::INVALID_THREAD_ID) {
            unpublish_process(child);
            close_all_fds(child);
            free_fd_table(child);
            (void)teardown_process_address_space(child);
            release_file_backed_vmas(child);
            bigos::free_pages(child->kernel_stack_base);
            free_process_object(child);
            return -bigos::ENOMEM;
        }

        return (int64_t)child_pid;
    }

    int64_t install_fd_current(bigos::vfs::File *__file, bool __close_on_exec) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __file == nullptr)
            return -bigos::EBADF;
        // Reuse the lowest free slot within the current capacity.
        for (uint32_t i = 0; i < process->fd_capacity; i++) {
            if (process->fd_table[i].file == nullptr) {
                process->fd_table[i].file = __file;
                process->fd_table[i].close_on_exec = __close_on_exec;
                process->fd_table[i].readable = __file->readable;
                return (int64_t)i;
            }
        }
        // No free slot: grow by one entry (bounded by the soft limit). The first
        // newly added index becomes the lowest available fd.
        const uint32_t fd = process->fd_capacity;
        if (fd >= bigos::proc::MAX_FDS_SOFT_LIMIT || !grow_fd_table(process, fd + 1))
            return -bigos::EMFILE;
        process->fd_table[fd].file = __file;
        process->fd_table[fd].close_on_exec = __close_on_exec;
        process->fd_table[fd].readable = __file->readable;
        return (int64_t)fd;
    }

    bigos::vfs::Status read_fd_current(uint32_t __fd, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr || !entry->readable)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::read(entry->file, __dst, __len, __bytes_read);
    }

    int64_t close_fd_current(uint32_t __fd) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return -bigos::EBADF;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return -bigos::EBADF;
        bigos::vfs::File *file = entry->file;
        entry->file = nullptr;
        entry->close_on_exec = false;
        entry->readable = false;
        bigos::vfs::release(file);
        return 0;
    }

    bigos::vfs::Status write_fd_current(
        uint32_t __fd, const void *__src, size_t __len, size_t *__bytes_written) noexcept {
        if (__bytes_written != nullptr)
            *__bytes_written = 0;
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::write(entry->file, __src, __len, __bytes_written);
    }

    bigos::vfs::Status lseek_fd_current(
        uint32_t __fd, int64_t __offset, int __whence, uint64_t *__new_offset) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::lseek(entry->file, __offset, __whence, __new_offset);
    }

    bigos::vfs::Status truncate_fd_current(uint32_t __fd, uint64_t __length) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::truncate(entry->file, __length);
    }

    bigos::vfs::Status fsync_fd_current(uint32_t __fd) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::fsync(entry->file);
    }

    bigos::vfs::Status readdir_fd_current(
        uint32_t __fd, bigos::vfs::DirectoryEntry *__entries, size_t __max_entries, size_t *__entries_read) noexcept {
        if (__entries_read != nullptr)
            *__entries_read = 0;
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::readdir(entry->file, __entries, __max_entries, __entries_read);
    }

    bigos::vfs::Status stat_fd_current(uint32_t __fd, bigos::Metadata *__out) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::stat(entry->file, __out);
    }

    bigos::vfs::File *file_for_fd_current(uint32_t __fd) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return nullptr;
        return process->fd_table[__fd].file;
    }

    int64_t dup_fd_current(uint32_t __oldfd) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __oldfd >= process->fd_capacity)
            return -bigos::EBADF;
        FdEntry *old = &process->fd_table[__oldfd];
        if (old->file == nullptr)
            return -bigos::EBADF;
        bigos::vfs::File *file = old->file;
        const bool readable = old->readable;
        // install_fd_current uses file->readable, so retain then install. The new
        // fd never inherits close-on-exec (POSIX dup semantics).
        bigos::vfs::retain(file);
        const int64_t fd = install_fd_current(file, false);
        if (fd < 0) {
            bigos::vfs::release(file);
            return fd;
        }
        process->fd_table[fd].readable = readable;
        return fd;
    }

    int64_t dup2_fd_current(uint32_t __oldfd, uint32_t __newfd) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __oldfd >= process->fd_capacity)
            return -bigos::EBADF;
        FdEntry *old = &process->fd_table[__oldfd];
        if (old->file == nullptr)
            return -bigos::EBADF;
        if (__newfd >= bigos::proc::MAX_FDS_SOFT_LIMIT)
            return -bigos::EBADF;
        // dup2(fd, fd) on a valid fd is a no-op returning newfd.
        if (__oldfd == __newfd)
            return (int64_t)__newfd;
        // Ensure the table is large enough to address newfd.
        if (__newfd >= process->fd_capacity && !grow_fd_table(process, __newfd + 1))
            return -bigos::EMFILE;

        bigos::vfs::File *file = old->file;
        const bool readable = old->readable;
        FdEntry *target = &process->fd_table[__newfd];
        if (target->file != nullptr) {
            bigos::vfs::File *prev = target->file;
            target->file = nullptr;
            bigos::vfs::release(prev);
        }
        bigos::vfs::retain(file);
        target->file = file;
        target->readable = readable;
        target->close_on_exec = false;
        return (int64_t)__newfd;
    }

    int64_t fcntl_fd_current(uint32_t __fd, int __cmd, uint64_t __arg) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr || process->state != ProcessState::Running || __fd >= process->fd_capacity)
            return -bigos::EBADF;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return -bigos::EBADF;

        switch (__cmd) {
            case bigos::proc::FCNTL_F_GETFD:
                return entry->close_on_exec ? bigos::proc::FCNTL_FD_CLOEXEC : 0;
            case bigos::proc::FCNTL_F_SETFD:
                if ((__arg & ~(uint64_t)bigos::proc::FCNTL_FD_CLOEXEC) != 0)
                    return -bigos::EINVAL;
                entry->close_on_exec = (__arg & bigos::proc::FCNTL_FD_CLOEXEC) != 0;
                return 0;
            case bigos::proc::FCNTL_F_GETFL:
                // Synthesized access mode (from the open file's readable/writable
                // state) ORed with O_NONBLOCK when the shared open file
                // description has the nonblocking flag set. Leaves fd, offset,
                // reference, and close-on-exec unchanged.
                return (int64_t)(bigos::vfs::file_access_mode(entry->file) |
                    (bigos::vfs::file_is_nonblocking(entry->file) ? bigos::proc::O_NONBLOCK : 0));
            case bigos::proc::FCNTL_F_SETFL:
                // Apply only the O_NONBLOCK bit to the open file description.
                // Every other bit (access-mode bits, creation bits, unimplemented
                // status bits) is ignored without error so the standard idiom
                // F_SETFL(F_GETFL | O_NONBLOCK) -- whose arg carries access-mode
                // bits -- still succeeds. Does not touch the access mode or
                // FdEntry.close_on_exec.
                entry->file->nonblocking = (__arg & bigos::proc::O_NONBLOCK) != 0;
                return 0;
            case bigos::proc::FCNTL_F_DUPFD: {
                if (__arg >= bigos::proc::MAX_FDS_SOFT_LIMIT)
                    return -bigos::EINVAL;
                bigos::vfs::File *file = entry->file;
                const bool readable = entry->readable;
                const uint32_t min_fd = (uint32_t)__arg;
                uint32_t target_fd = min_fd;
                while (target_fd < process->fd_capacity && process->fd_table[target_fd].file != nullptr)
                    target_fd++;
                if (target_fd >= bigos::proc::MAX_FDS_SOFT_LIMIT)
                    return -bigos::EMFILE;
                if (target_fd >= process->fd_capacity && !grow_fd_table(process, target_fd + 1))
                    return -bigos::EMFILE;

                bigos::vfs::retain(file);
                FdEntry *target = &process->fd_table[target_fd];
                target->file = file;
                target->readable = readable;
                target->close_on_exec = false;
                return (int64_t)target_fd;
            }
            default:
                return -bigos::EINVAL;
        }
    }

    void close_all_fds(Process *__process) noexcept {
        if (__process == nullptr)
            return;
        for (uint32_t i = 0; i < __process->fd_capacity; i++) {
            FdEntry *entry = &__process->fd_table[i];
            if (entry->file == nullptr)
                continue;
            bigos::vfs::File *file = entry->file;
            entry->file = nullptr;
            entry->close_on_exec = false;
            entry->readable = false;
            bigos::vfs::release(file);
        }
    }

    // Releases the retained backing-file reference held by every file-backed VMA
    // and clears the pointer so a double release cannot occur. The process-owned
    // page-table entries (including any shared read-only cache frame, reference
    // counted) are reclaimed separately by teardown_user_address_space; this only
    // drops this process's hold on the vfs::File, never on shared cache state.
    void release_file_backed_vmas(Process *__process) noexcept {
        if (__process == nullptr)
            return;
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            VmaEntry &entry = __process->vmas.entries[i];
            if (entry.used && entry.backing == VmaBacking::FileBacked && entry.file_backing != nullptr) {
                bigos::vfs::File *file = entry.file_backing;
                entry.file_backing = nullptr;
                bigos::vfs::release(file);
            }
        }
    }

    void close_on_exec_fds(Process *__process) noexcept {
        if (__process == nullptr)
            return;
        for (uint32_t i = 0; i < __process->fd_capacity; i++) {
            FdEntry *entry = &__process->fd_table[i];
            if (entry->file == nullptr || !entry->close_on_exec)
                continue;
            bigos::vfs::File *file = entry->file;
            entry->file = nullptr;
            entry->close_on_exec = false;
            entry->readable = false;
            bigos::vfs::release(file);
        }
    }

    void mark_current_faulted(int64_t __reason) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr)
            return;
        process->state = ProcessState::Faulted;
        process->exit_code = __reason;
        process->fault_reason = __reason;
        // Reparent any surviving children to PID-1 init before this process
        // becomes a zombie, so their later exits are reaped by init (decision 9).
        reparent_children_to_init(process);
        mark_zombie_or_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_PAGE_FAULT\n");
        activate_process_kernel_address_space(process);
    }

    [[noreturn]] void fault_current_and_exit(int64_t __reason) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr)
            halt_failed("BIGOS_USER_FAULT_FAILED no-process\n");

        // Once the process becomes waitable, keep this CPU on the exiting thread
        // until sched::thread_exit() switches away. Otherwise a timer preemption
        // can let the parent reap and free this process kernel stack while this
        // thread is still running on it.
        bigos::irq::disableIRQ();
        mark_current_faulted(__reason);
        set_current_process_slot(nullptr);
        bigos::sched::set_current_user_process(nullptr);
        bigos::sched::thread_exit();
    }

    [[noreturn]] void exit_current(int64_t __code) noexcept {
        Process *process = current_process_slot();
        if (process == nullptr)
            halt_failed("BIGOS_USER_EXIT_FAILED no-process\n");

        // Once the process becomes waitable, keep this CPU on the exiting thread
        // until sched::thread_exit() switches away. Otherwise a timer preemption
        // can let the parent reap and free this process kernel stack while this
        // thread is still running on it.
        bigos::irq::disableIRQ();
        process->state = ProcessState::Terminated;
        process->exit_code = __code;
        // Reparent any surviving children to PID-1 init before this process
        // becomes a zombie, so their later exits are reaped by init (decision 9).
        reparent_children_to_init(process);
        mark_zombie_or_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_EXIT\n");
        // Default-on init normal exit: emit the BIGOS_INIT_EXIT marker and fall
        // through to the existing deferred-reap/idle path (decision 5: idle, not
        // panic). init itself is the resident PID-1 and is not expected to exit.
        if (process == g_init_process)
            bigos::serial_puts("BIGOS_INIT_EXIT\n");
        activate_process_kernel_address_space(process);
        set_current_process_slot(nullptr);
        bigos::sched::set_current_user_process(nullptr);
        bigos::sched::thread_exit();
    }

    namespace {
        bool process_is_controllable(const Process *process) noexcept {
            return process != nullptr && process->table_published && process->pid != 0 &&
                   process->state != ProcessState::Terminated && process->state != ProcessState::Faulted &&
                   process->state != ProcessState::Zombie && process->state != ProcessState::ReapPending &&
                   process->state != ProcessState::Reaped;
        }

        bool is_child_of(const Process *parent, uint32_t child_pid) noexcept {
            if (parent == nullptr || child_pid == 0)
                return false;
            uint32_t pid = parent->first_child_pid;
            while (pid != 0) {
                Process *child = lookup_process(pid);
                if (child == nullptr)
                    return false;
                if (child->pid == child_pid)
                    return true;
                pid = child->next_sibling_pid;
            }
            return false;
        }
    }   // namespace

    int64_t getpgid_current(uint32_t __pid) noexcept {
        const Process *actor = current_process_slot();
        if (actor == nullptr)
            return -bigos::ESRCH;
        const uint32_t pid = __pid == 0 ? actor->pid : __pid;
        const Process *target = lookup_process(pid);
        if (!process_is_controllable(target))
            return -bigos::ESRCH;
        return (int64_t)target->pgid;
    }

    int64_t getsid_current(uint32_t __pid) noexcept {
        const Process *actor = current_process_slot();
        if (actor == nullptr)
            return -bigos::ESRCH;
        const uint32_t pid = __pid == 0 ? actor->pid : __pid;
        const Process *target = lookup_process(pid);
        if (!process_is_controllable(target))
            return -bigos::ESRCH;
        return (int64_t)target->sid;
    }

    bool process_group_exists_in_session(uint32_t __pgid, uint32_t __sid) noexcept {
        if (__pgid == 0 || __sid == 0)
            return false;
        for (Process *process = g_process_list_head; process != nullptr; process = process->reg_next) {
            if (process_is_controllable(process) && process->pgid == __pgid && process->sid == __sid)
                return true;
        }
        return false;
    }

    bool process_group_has_live_member(uint32_t __pgid) noexcept {
        if (__pgid == 0)
            return false;
        for (Process *process = g_process_list_head; process != nullptr; process = process->reg_next) {
            if (process_is_controllable(process) && process->pgid == __pgid)
                return true;
        }
        return false;
    }

    int64_t setpgid_current(uint32_t __pid, uint32_t __pgid) noexcept {
        Process *actor = current_process_slot();
        if (actor == nullptr)
            return -bigos::ESRCH;

        const uint32_t target_pid = __pid == 0 ? actor->pid : __pid;
        Process *target = lookup_process(target_pid);
        if (!process_is_controllable(target))
            return -bigos::ESRCH;
        if (target != actor && !is_child_of(actor, target_pid))
            return -bigos::EPERM;
        if (target->sid != actor->sid)
            return -bigos::EPERM;

        const uint32_t pgid = __pgid == 0 ? target->pid : __pgid;
        if (pgid == 0 || pgid == WAIT_ANY)
            return -bigos::EINVAL;
        if (target->pid == target->sid && pgid != target->pid)
            return -bigos::EPERM;
        if (pgid != target->pid && !process_group_exists_in_session(pgid, actor->sid))
            return -bigos::EPERM;

        target->pgid = pgid;
        return 0;
    }

    int64_t setsid_current() noexcept {
        Process *process = current_process_slot();
        if (!process_is_controllable(process))
            return -bigos::ESRCH;
        if (process->pgid == process->pid)
            return -bigos::EPERM;
        process->sid = process->pid;
        process->pgid = process->pid;
        return (int64_t)process->sid;
    }

    int64_t signal_process_group_from_current(uint32_t __pgid, int __signo) noexcept {
        Process *actor = current_process_slot();
        if (actor == nullptr)
            return -bigos::ESRCH;
        if (__pgid == 0)
            return -bigos::EINVAL;
        int64_t delivered = 0;
        bool denied = false;
        for (Process *target = g_process_list_head; target != nullptr; target = target->reg_next) {
            if (!process_is_controllable(target) || target->pgid != __pgid)
                continue;
            if (!bigos::cred::may_signal(actor, target)) {
                denied = true;
                continue;
            }
            const int64_t result = bigos::signal::kill(target, __signo);
            if (result != 0)
                return result;
            delivered++;
        }
        if (delivered == 0)
            return denied ? -bigos::EPERM : -bigos::ESRCH;
        return 0;
    }

    int64_t wait_current(uint32_t __pid, int64_t *__status, uint32_t __options) noexcept {
        Process *parent = current_process_slot();
        if (parent == nullptr)
            return -bigos::ECHILD;
        if (!bigos::sched::can_block())
            return -bigos::EWOULDBLOCK;
        if ((__options & ~bigos::proc::WAIT_OPTION_WNOHANG) != 0)
            return -bigos::EINVAL;
        if (__pid == 0 || ((__pid & 0x80000000u) != 0 && __pid != WAIT_ANY))
            return -bigos::EINVAL;

        Process *match = nullptr;
        for (;;) {
            uint32_t child_pid = parent->first_child_pid;
            bool has_matching_child = false;
            while (child_pid != 0) {
                Process *child = lookup_process(child_pid);
                if (child == nullptr)
                    break;
                if (__pid == WAIT_ANY || child->pid == __pid) {
                    has_matching_child = true;
                    if (child->state == ProcessState::Zombie || child->state == ProcessState::Terminated ||
                        child->state == ProcessState::Faulted) {
                        match = child;
                        break;
                    }
                }
                child_pid = child->next_sibling_pid;
            }

            if (match != nullptr)
                break;
            if (!has_matching_child)
                return -bigos::ECHILD;
            if ((__options & bigos::proc::WAIT_OPTION_WNOHANG) != 0)
                return 0;

            parent->parent_waiting = true;
            const int wait_result = bigos::sched::wait_queue_wait_until(&g_process_wait_queue, nullptr, nullptr, 0);
            parent->parent_waiting = false;
            if (wait_result == bigos::sched::WAIT_BLOCK_FORBIDDEN)
                return -bigos::EWOULDBLOCK;
            if (wait_result != bigos::sched::WAIT_OK)
                return -bigos::EINVAL;
        }

        if (__status != nullptr)
            *__status = match->exit_code;
        const uint32_t waited_pid = match->pid;
        match->wait_status_consumed = true;
        mark_reap_pending(match);
        reap_pending_processes();
        return (int64_t)waited_pid;
    }

    int64_t wait_current(uint32_t __pid, int64_t *__status) noexcept {
        return wait_current(__pid, __status, 0);
    }

    void reap_pending_processes() noexcept {
        uint32_t *link = &g_reap_head_pid;
        while (*link != 0) {
            Process *process = lookup_process(*link);
            if (process == nullptr) {
                *link = 0;
                continue;
            }
            if (!process->reap_pending || process->resources_reclaimed) {
                link = &process->next_reap_pid;
                continue;
            }
            if (stack_is_active(process)) {
                bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-stack\n");
                link = &process->next_reap_pid;
                continue;
            }
            if (bigos::arch::vm_user::is_active_address_space(process->address_space_root) ||
                bigos::mm::mm_context_active_cpu_mask(process->mm_context) != 0) {
                bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-root\n");
                link = &process->next_reap_pid;
                continue;
            }

            if (process->address_space_root != bigos::mm::INVALID_PHYS_ADDR) {
                if (!teardown_process_address_space(process)) {
                    bigos::serial_puts("BIGOS_USER_REAP_FAILED address-space\n");
                    link = &process->next_reap_pid;
                    continue;
                }
            }

            close_all_fds(process);
            free_fd_table(process);
            release_file_backed_vmas(process);

            if (process->kernel_stack_base != nullptr) {
                bigos::free_pages(process->kernel_stack_base);
                process->kernel_stack_base = nullptr;
                process->kernel_stack_len = 0;
                process->kernel_stack_top = 0;
            }

            process->resources_reclaimed = true;
            process->reap_pending = false;
            process->state = ProcessState::Reaped;
            *link = process->next_reap_pid;
            process->next_reap_pid = 0;
            if (process == g_init_process)
                g_init_process = nullptr;
            unpublish_process(process);
            bigos::serial_puts("BIGOS_USER_RECLAIMED\n");
            // Final step: the object is no longer referenced by current, the reap
            // chain, the registry, or any parent/child link. Free its heap
            // backing (static smoke objects keep heap_allocated == false and are
            // left untouched). MUST be the last use of `process`.
            free_process_object(process);
        }
    }

#ifdef BIGOS_USER_PROGRAM_SMOKE
    void user_program_smoke_entry(void *) noexcept {
        static Process first_process;
        if (!create_first_user_process(&first_process))
            halt_failed("BIGOS_USER_LOAD_FAILED create\n");
        run_user_process(&first_process);
    }
#endif

#ifdef BIGOS_DEMAND_PAGING_SMOKE
    // Validation-only, default-off demand-paging smoke. Runs from ordinary kernel
    // thread context (can_block() true) and drives the unified user page-fault
    // entry against a constructed process without entering ring3. It covers:
    //   1. lazy materialization hit on a registered heap page,
    //   2. allocation-failure kill (injected via the smoke-only hook),
    //   3. permission-violation kill (present/protection-violation bit set).
    // The process address-space root is inactive here, so map/teardown target the
    // process root only and never touch the active kernel CR3 or VGA/MMIO.
    bool demand_paging_smoke_run(Process *__process) noexcept {
        if (!create_first_user_process(__process))
            return false;

        set_current_process_slot(__process);
        __process->state = ProcessState::Running;

        const uint64_t heap_base = __process->vmas.heap_base;
        const uint64_t write_user_fault = 0x2 | 0x4;   // write + user, present=0

        bool ok = true;

        // 1. Lazy materialization hit: grow heap lazily, then fault the first
        // covered page and confirm it gets mapped in the process root.
        if (brk_current(heap_base + 2 * PAGE_SIZE) != (int64_t)(heap_base + 2 * PAGE_SIZE))
            ok = false;
        if (ok && !try_handle_user_page_fault(heap_base, write_user_fault))
            ok = false;
        if (ok && !bigos::mm::user_range_mapped(__process->address_space_root, heap_base, PAGE_SIZE))
            ok = false;

        // 2. Allocation-failure kill: a legitimate fault on the next registered
        // heap page must fail deterministically when no frame is available and
        // must not publish a mapping.
        if (ok) {
            g_demand_paging_force_alloc_fail = true;
            const bool recovered = try_handle_user_page_fault(heap_base + PAGE_SIZE, write_user_fault);
            g_demand_paging_force_alloc_fail = false;
            if (recovered)
                ok = false;
            if (ok && bigos::mm::user_range_mapped(__process->address_space_root, heap_base + PAGE_SIZE, PAGE_SIZE))
                ok = false;
        }

        // 3. Permission-violation kill: a present/protection-violation fault is
        // never a demand-zero candidate and must not be recovered.
        if (ok) {
            const uint64_t protection_fault = 0x1 | 0x2 | 0x4;   // present + write + user
            if (try_handle_user_page_fault(heap_base, protection_fault))
                ok = false;
        }

        // Out-of-range fault outside any VMA must also be rejected.
        if (ok && try_handle_user_page_fault(0x2000000ull, write_user_fault))
            ok = false;

        // Deterministic teardown of the constructed process through the existing
        // lifecycle/reaper; the inactive root is reclaimed by reap.
        set_current_process_slot(nullptr);
        __process->state = ProcessState::Terminated;
        mark_zombie_or_reap_pending(__process);
        reap_pending_processes();
        return ok;
    }

    void demand_paging_smoke_entry(void *) noexcept {
        static Process smoke_process;
        if (demand_paging_smoke_run(&smoke_process))
            bigos::serial_puts("BIGOS_DEMAND_PAGING_PASSED\n");
        else
            bigos::serial_puts("BIGOS_DEMAND_PAGING_FAILED\n");
    }
#endif

#ifdef BIGOS_FILE_BACKED_MAPPING_SMOKE
    // Validation-only, default-off bounded read-only file-backed mapping smoke.
    // Runs from ordinary kernel-thread context (can_block() true) and drives the
    // mapping request and unified user page-fault entry against a constructed
    // process backed by a synthetic in-RAM read-only file, without entering ring3.
    // The process address-space root is inactive here, so map/teardown target the
    // process root only and never touch the active kernel CR3 or VGA/MMIO. It
    // covers: mapping creation, first-access materialization observing correct
    // file content, file-tail-page zero-fill, out-of-range deterministic kill, and
    // write-to-read-only deterministic kill.
    namespace filemap_smoke {
        // Synthetic read-only file backed by a static RAM buffer. The file is one
        // page plus a partial second page, so the third mapped page is entirely
        // beyond the file length (full zero) and the second page is a file-tail
        // page (partial content + zero-fill).
        constexpr uint64_t kFileSize = PAGE_SIZE + 100;
        alignas(PAGE_SIZE) static uint8_t g_file_data[PAGE_SIZE * 2];
        static bigos::vfs::Vnode g_vnode;

        bigos::vfs::Status read_op(
            bigos::vfs::File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
            if (__bytes_read != nullptr)
                *__bytes_read = 0;
            if (__file == nullptr || __dst == nullptr)
                return bigos::vfs::Status::BadFileDescriptor;
            const uint64_t offset = __file->offset;
            if (offset >= kFileSize)
                return bigos::vfs::Status::Success;   // 0 bytes at/after EOF
            uint64_t avail = kFileSize - offset;
            uint64_t n = __len < avail ? __len : avail;
            memcpy(__dst, g_file_data + offset, n);
            __file->offset += n;
            if (__bytes_read != nullptr)
                *__bytes_read = (size_t)n;
            return bigos::vfs::Status::Success;
        }

        void close_op(bigos::vfs::File *) noexcept {}

        const bigos::vfs::FileOperations g_ops = {read_op, close_op, nullptr, nullptr, nullptr, nullptr, nullptr};
    }   // namespace filemap_smoke

    bool file_backed_mapping_smoke_run(Process *__process) noexcept {
        using namespace filemap_smoke;
        if (!create_first_user_process(__process))
            return false;

        // Initialize the synthetic file content: byte i = (i & 0xff) for the
        // in-file region; the rest of the static buffer is the zero-fill source.
        for (uint64_t i = 0; i < kFileSize; i++)
            g_file_data[i] = (uint8_t)(i & 0xff);
        for (uint64_t i = kFileSize; i < sizeof(g_file_data); i++)
            g_file_data[i] = 0;

        // A heap-allocated read-only vfs::File so install/map retain/release
        // counting matches the real path; its close op is a no-op and release
        // frees the object once the last reference drops.
        auto *file = (bigos::vfs::File *)bigos::kmalloc(sizeof(bigos::vfs::File));
        if (file == nullptr)
            return false;
        memset(file, 0, sizeof(*file));
        g_vnode.size = kFileSize;
        g_vnode.is_directory = false;
        g_vnode.private_data = nullptr;
        file->ops = &g_ops;
        file->vnode = &g_vnode;
        file->offset = 0;
        file->ref_count = 1;
        file->readable = true;
        file->identity = {bigos::vfs::FILE_BACKEND_BIGFS, 0x534d4f4bu, 0x1000u};

        set_current_process_slot(__process);
        __process->state = ProcessState::Running;

        bool ok = true;
        uint64_t first_phys = bigos::mm::INVALID_PHYS_ADDR;
        const int64_t fd = install_fd_current(file, false);
        if (fd < 0) {
            bigos::vfs::release(file);
            ok = false;
        }

        const uint64_t map_len = 3 * PAGE_SIZE;
        int64_t mapped = -1;
        if (ok) {
            mapped = map_file_current((uint64_t)fd, 0, map_len, permissions_value(VmaPermission::Read), 0);
            if (mapped < 0)
                ok = false;
        }
        const uint64_t base = (uint64_t)mapped;
        const uint64_t read_user_fault = 0x4;   // user, present=0, read

        // 1. First-access materialization of the first full in-file page.
        if (ok && !try_handle_user_page_fault(base, read_user_fault))
            ok = false;
        if (ok) {
            uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
            bigos::mm::PageAttr attr = 0;
            if (!bigos::mm::read_user_leaf_in_root(__process->address_space_root, base, &phys, &attr))
                ok = false;
            first_phys = phys;
            // Must be present, user, read-only (not writable), non-executable.
            if (ok && ((attr & bigos::mm::page_attr::PRESENT) == 0 || (attr & bigos::mm::page_attr::USER) == 0 ||
                          (attr & bigos::mm::page_attr::WRITABLE) != 0 ||
                          (attr & bigos::mm::page_attr::NO_EXECUTE) == 0))
                ok = false;
            if (ok) {
                const uint8_t *page = (const uint8_t *)bigos::mm::phys_to_direct(phys);
                if (page == nullptr || page[0] != 0 || page[1] != 1 || page[255] != 255)
                    ok = false;
            }
        }

        // 2. A second process independently maps a distinct File object with the
        // same stable identity and reuses the already materialized shared frame.
        static Process shared_process;
        if (ok) {
            scrub_process(&shared_process);
            internal_init_vmas(&shared_process.vmas);
            if (!init_runtime_layout(&shared_process, USER_CODE_BASE, USER_DATA_BASE + PAGE_SIZE,
                    USER_DATA_BASE + PAGE_SIZE)) {
                bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED shared-layout\n");
                ok = false;
            }
            shared_process.address_space_root = bigos::mm::derive_user_address_space_root();
            shared_process.kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
            init_fd_table(&shared_process);
            if (shared_process.address_space_root == bigos::mm::INVALID_PHYS_ADDR) {
                bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED shared-root\n");
                ok = false;
            }
        }
        if (ok) {
            auto *file2 = (bigos::vfs::File *)bigos::kmalloc(sizeof(bigos::vfs::File));
            if (file2 == nullptr) {
                ok = false;
            } else {
                memset(file2, 0, sizeof(*file2));
                file2->ops = &g_ops;
                file2->vnode = &g_vnode;
                file2->offset = 0;
                file2->ref_count = 1;
                file2->readable = true;
                file2->identity = file->identity;
                set_current_process_slot(&shared_process);
                shared_process.state = ProcessState::Running;
                const int64_t fd2 = install_fd_current(file2, false);
                const int64_t mapped2 =
                    fd2 >= 0 ? map_file_current((uint64_t)fd2, 0, PAGE_SIZE, permissions_value(VmaPermission::Read), 0) :
                               -1;
                if (fd2 < 0) {
                    bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED shared-fd\n");
                    bigos::vfs::release(file2);
                    ok = false;
                } else if (mapped2 < 0 || !try_handle_user_page_fault((uint64_t)mapped2, read_user_fault)) {
                    bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED shared-map\n");
                    ok = false;
                } else {
                    uint64_t phys2 = bigos::mm::INVALID_PHYS_ADDR;
                    if (!bigos::mm::read_user_leaf_in_root(
                            shared_process.address_space_root, (uint64_t)mapped2, &phys2, nullptr) ||
                        phys2 != first_phys) {
                        bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED shared-phys\n");
                        ok = false;
                    }
                }
            }
            set_current_process_slot(nullptr);
            close_all_fds(&shared_process);
            free_fd_table(&shared_process);
            release_file_backed_vmas(&shared_process);
            if (shared_process.address_space_root != bigos::mm::INVALID_PHYS_ADDR) {
                (void)unmap_shared_file_backed_pages(shared_process.address_space_root, &shared_process.vmas);
                (void)bigos::mm::teardown_user_address_space(shared_process.address_space_root);
                shared_process.address_space_root = bigos::mm::INVALID_PHYS_ADDR;
            }
            set_current_process_slot(__process);
            __process->state = ProcessState::Running;
        }

        // 3. File-tail page zero-fill: second page holds 100 in-file bytes then zeros.
        if (ok && !try_handle_user_page_fault(base + PAGE_SIZE, read_user_fault))
            ok = false;
        if (ok) {
            uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
            bigos::mm::PageAttr attr = 0;
            if (!bigos::mm::read_user_leaf_in_root(__process->address_space_root, base + PAGE_SIZE, &phys, &attr))
                ok = false;
            if (ok) {
                const uint8_t *page = (const uint8_t *)bigos::mm::phys_to_direct(phys);
                // In-file byte 0 of this page is file offset PAGE_SIZE -> value
                // (PAGE_SIZE & 0xff) == 0; byte 99 is the last in-file byte;
                // byte 100 and beyond must be zero-filled.
                if (page == nullptr || page[99] != (uint8_t)((PAGE_SIZE + 99) & 0xff) || page[100] != 0 ||
                    page[PAGE_SIZE - 1] != 0)
                    ok = false;
            }
        }

        // 4. Third page is fully beyond the file length but within the VMA:
        // materializes as an all-zero read-only page.
        if (ok && !try_handle_user_page_fault(base + 2 * PAGE_SIZE, read_user_fault))
            ok = false;

        // 5. Write to a read-only file-backed page is a deterministic kill (not
        // COW, not materialized), for both present and not-present write faults.
        if (ok) {
            const uint64_t write_present_fault = 0x1 | 0x2 | 0x4;   // present + write + user
            if (try_handle_user_page_fault(base, write_present_fault))
                ok = false;
        }
        if (ok) {
            const uint64_t write_notpresent_fault = 0x2 | 0x4;   // not-present + write + user
            if (try_handle_user_page_fault(base + 2 * PAGE_SIZE, write_notpresent_fault))
                ok = false;
        }

        // 6. Out-of-range access beyond the mapped VMA is a deterministic kill.
        if (ok && try_handle_user_page_fault(base + map_len, read_user_fault))
            ok = false;

        // Deterministic teardown through the existing lifecycle/reaper; this also
        // releases the file-backed VMA reference and the installed fd.
        set_current_process_slot(nullptr);
        __process->state = ProcessState::Terminated;
        mark_zombie_or_reap_pending(__process);
        reap_pending_processes();
        return ok;
    }

    void file_backed_mapping_smoke_entry(void *) noexcept {
        static Process smoke_process;
        if (file_backed_mapping_smoke_run(&smoke_process))
            bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_PASSED\n");
        else
            bigos::serial_puts("BIGOS_FILE_BACKED_MAPPING_FAILED\n");
    }
#endif

#ifdef BIGOS_GROWABLE_TABLES_SMOKE
    // Validation-only, default-off growable process/fd table smoke. Runs from
    // ordinary kernel-thread context (can_block() true, non-IRQ) so kmalloc/free
    // are legal. It deliberately exercises only the growable registry and fd
    // storage (no ring3 entry / address-space machinery) and covers:
    //   1. registering more processes than the former fixed 16-process cap,
    //      with stable distinct PIDs and lookup, then slot/PID reuse after the
    //      objects are unpublished and freed,
    //   2. installing more fds than the former fixed 16-fd cap with ascending
    //      lowest-free allocation, plus lowest-free reuse after a close,
    //   3. deterministic degradation under injected allocation failure
    //      (process-object alloc -> nullptr, fd growth -> EMFILE), no panic.
    bigos::vfs::File *growable_make_dummy_file() noexcept {
        auto *file = (bigos::vfs::File *)bigos::kmalloc(sizeof(bigos::vfs::File));
        if (file == nullptr)
            return nullptr;
        memset(file, 0, sizeof(*file));
        file->ops = nullptr;
        file->vnode = nullptr;
        file->offset = 0;
        file->ref_count = 1;
        file->readable = true;
        file->close_on_exec = false;
        return file;
    }

    bool growable_tables_smoke_run() noexcept {
        if (!g_proc_initialized)
            init();

        Process *saved_current = current_process_slot();
        bool ok = true;

        // 1. Process registry growth beyond the former fixed 16-process cap.
        constexpr uint32_t kProcessCount = 20;
        const uint32_t base_count = g_process_count;
        Process *procs[kProcessCount] = {};
        for (uint32_t i = 0; i < kProcessCount && ok; i++) {
            procs[i] = alloc_process_object();
            if (procs[i] == nullptr || !publish_process(procs[i], ROOT_PARENT_PID))
                ok = false;
        }
        for (uint32_t i = 0; i < kProcessCount && ok; i++) {
            if (procs[i] == nullptr || procs[i]->pid == 0 || lookup_process(procs[i]->pid) != procs[i])
                ok = false;
        }
        if (ok && g_process_count != base_count + kProcessCount)
            ok = false;
        // Unpublish + free: registry slots and PIDs must become reusable.
        for (uint32_t i = 0; i < kProcessCount; i++) {
            if (procs[i] == nullptr)
                continue;
            const uint32_t pid = procs[i]->pid;
            unpublish_process(procs[i]);
            if (ok && lookup_process(pid) != nullptr)
                ok = false;
            free_process_object(procs[i]);
            procs[i] = nullptr;
        }
        if (ok && g_process_count != base_count)
            ok = false;

        // 2. fd table growth beyond the former fixed 16-fd cap.
        if (ok) {
            Process *fdproc = alloc_process_object();
            if (fdproc == nullptr) {
                ok = false;
            } else {
                fdproc->state = ProcessState::Running;
                init_fd_table(fdproc);
                set_current_process_slot(fdproc);

                constexpr uint32_t kFdCount = 20;
                for (uint32_t i = 0; i < kFdCount && ok; i++) {
                    bigos::vfs::File *file = growable_make_dummy_file();
                    if (file == nullptr) {
                        ok = false;
                        break;
                    }
                    const int64_t fd = install_fd_current(file, false);
                    if (fd != (int64_t)i) {
                        bigos::vfs::release(file);
                        ok = false;
                    }
                }
                // Close a middle fd and confirm the lowest-free slot is reused.
                if (ok) {
                    if (close_fd_current(5) != 0) {
                        ok = false;
                    } else {
                        bigos::vfs::File *file = growable_make_dummy_file();
                        if (file == nullptr || install_fd_current(file, false) != 5) {
                            if (file != nullptr)
                                bigos::vfs::release(file);
                            ok = false;
                        }
                    }
                }
                close_all_fds(fdproc);
                free_fd_table(fdproc);
                set_current_process_slot(nullptr);
                free_process_object(fdproc);
            }
        }

        // 3. Deterministic degradation under injected allocation failure.
        if (ok) {
            g_growable_force_alloc_fail = true;
            if (alloc_process_object() != nullptr)
                ok = false;

            static Process inject_proc;
            scrub_process(&inject_proc);
            inject_proc.state = ProcessState::Running;
            init_fd_table(&inject_proc);
            set_current_process_slot(&inject_proc);
            bigos::vfs::File *file = growable_make_dummy_file();
            if (file == nullptr) {
                ok = false;
            } else {
                // No capacity yet and growth injection forced to fail -> EMFILE.
                if (install_fd_current(file, false) != -bigos::EMFILE)
                    ok = false;
                bigos::vfs::release(file);
            }
            free_fd_table(&inject_proc);
            set_current_process_slot(nullptr);
            g_growable_force_alloc_fail = false;
        }

        g_growable_force_alloc_fail = false;
        set_current_process_slot(saved_current);
        return ok;
    }

    void growable_tables_smoke_entry(void *) noexcept {
        if (growable_tables_smoke_run())
            bigos::serial_puts("BIGOS_GROWABLE_TABLES_PASSED\n");
        else
            bigos::serial_puts("BIGOS_GROWABLE_TABLES_FAILED\n");
    }
#endif

#ifdef BIGOS_FORK_COW_SMOKE
    // Validation-only, default-off fork/copy-on-write smoke. Runs from ordinary
    // kernel-thread context (can_block() true, non-IRQ) so kmalloc/free and frame
    // allocation are legal. It exercises the COW machinery against inactive
    // process roots without entering ring3, covering:
    //   1. fork-style COW clone: a writable anonymous page becomes read-only +
    //      PTE_COW in both parent and child over one shared, reference-counted
    //      frame (parent/child can run independently from the same image),
    //   2. write-time split memory isolation: a write to the shared page in one
    //      process allocates a private copy and leaves the other untouched,
    //   3. lone-owner in-place restore plus reference-counted release on ordered
    //      teardown (no early free, no leak),
    //   4. allocation-failure deterministic degradation: an injected frame-alloc
    //      failure makes the write split fail without corrupting the shared frame
    //      or leaving a writable mapping.
    bool fork_cow_leaf(Process *__process, uint64_t __vaddr, uint64_t *__phys, bigos::mm::PageAttr *__attr) noexcept {
        return bigos::mm::read_user_leaf_in_root(__process->address_space_root, __vaddr, __phys, __attr);
    }

    bool fork_cow_smoke_run() noexcept {
        if (!g_proc_initialized)
            init();
        Process *saved_current = current_process_slot();
        bool ok = true;

        static Process parent;
        static Process child;
        scrub_process(&parent);
        scrub_process(&child);

        if (!create_first_user_process(&parent))
            return false;
        set_current_process_slot(&parent);
        parent.state = ProcessState::Running;

        const uint64_t data_page = USER_DATA_BASE;

        uint64_t parent_phys0 = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr parent_attr0 = 0;
        if (!fork_cow_leaf(&parent, data_page, &parent_phys0, &parent_attr0) ||
            (parent_attr0 & bigos::mm::page_attr::WRITABLE) == 0)
            ok = false;

        // Independent child kernel stack so clone_process_kernel_stack_mapping has
        // something to mirror, matching the real fork path.
        child.kernel_stack_base = ok ? bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING) : nullptr;
        if (ok && child.kernel_stack_base == nullptr)
            ok = false;
        if (ok) {
            child.kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
            child.kernel_stack_top = (uint64_t)child.kernel_stack_base + child.kernel_stack_len;
            child.address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        }

        // 1. COW clone.
        if (ok && !clone_user_address_space_cow(&parent, &child))
            ok = false;
        if (ok && !clone_fd_table(&parent, &child))
            ok = false;
        if (ok)
            child.state = ProcessState::Running;

        // Both sides must now be read-only + PTE_COW over the same shared frame.
        uint64_t parent_phys1 = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr parent_attr1 = 0;
        uint64_t child_phys1 = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr child_attr1 = 0;
        if (ok && (!fork_cow_leaf(&parent, data_page, &parent_phys1, &parent_attr1) ||
                      !fork_cow_leaf(&child, data_page, &child_phys1, &child_attr1)))
            ok = false;
        if (ok && (parent_phys1 != parent_phys0 || child_phys1 != parent_phys0))
            ok = false;
        if (ok && ((parent_attr1 & bigos::mm::page_attr::WRITABLE) != 0 ||
                      (parent_attr1 & bigos::mm::page_attr::PTE_COW) == 0))
            ok = false;
        if (ok &&
            ((child_attr1 & bigos::mm::page_attr::WRITABLE) != 0 || (child_attr1 & bigos::mm::page_attr::PTE_COW) == 0))
            ok = false;
        if (ok && !bigos::mm::frame_ref_is_shared(parent_phys0))
            ok = false;

        // 2. Write-time split on the parent: shared -> private copy.
        const uint64_t cow_write_fault = 0x1 | 0x2 | 0x4;   // present + write + user
        if (ok) {
            set_current_process_slot(&parent);
            if (!try_handle_user_page_fault(data_page, cow_write_fault))
                ok = false;
        }
        uint64_t parent_phys2 = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr parent_attr2 = 0;
        uint64_t child_phys2 = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr child_attr2 = 0;
        if (ok && (!fork_cow_leaf(&parent, data_page, &parent_phys2, &parent_attr2) ||
                      !fork_cow_leaf(&child, data_page, &child_phys2, &child_attr2)))
            ok = false;
        // Parent now owns a fresh writable frame; child still maps the original.
        if (ok && ((parent_attr2 & bigos::mm::page_attr::WRITABLE) == 0 || parent_phys2 == parent_phys0))
            ok = false;
        if (ok && child_phys2 != parent_phys0)
            ok = false;
        // The original frame now has a single owner (the child).
        if (ok && bigos::mm::frame_ref_is_shared(parent_phys0))
            ok = false;

        // 3. Lone-owner in-place restore on the child: no copy, same frame.
        if (ok) {
            set_current_process_slot(&child);
            if (!try_handle_user_page_fault(data_page, cow_write_fault))
                ok = false;
        }
        uint64_t child_phys3 = bigos::mm::INVALID_PHYS_ADDR;
        bigos::mm::PageAttr child_attr3 = 0;
        if (ok && !fork_cow_leaf(&child, data_page, &child_phys3, &child_attr3))
            ok = false;
        if (ok && ((child_attr3 & bigos::mm::page_attr::WRITABLE) == 0 || child_phys3 != parent_phys0))
            ok = false;

        // 4. Allocation-failure deterministic degradation. Re-share the parent's
        // current data frame with a probe so a write split must copy, then inject
        // a frame-alloc failure and confirm the split is rejected without
        // corrupting the shared frame or making the page writable.
        if (ok) {
            if (!bigos::mm::frame_ref_inc(parent_phys2))
                ok = false;
            if (ok && !bigos::mm::remap_user_page_in_root(
                          parent.address_space_root, data_page, parent_phys2, cow_shared_attr(parent_attr2)))
                ok = false;
            if (ok) {
                set_current_process_slot(&parent);
                g_fork_cow_force_alloc_fail = true;
                const bool split = try_handle_user_page_fault(data_page, cow_write_fault);
                g_fork_cow_force_alloc_fail = false;
                if (split)
                    ok = false;
                uint64_t pp = bigos::mm::INVALID_PHYS_ADDR;
                bigos::mm::PageAttr pa = 0;
                if (ok && (!fork_cow_leaf(&parent, data_page, &pp, &pa) || pp != parent_phys2 ||
                              (pa & bigos::mm::page_attr::WRITABLE) != 0 || (pa & bigos::mm::page_attr::PTE_COW) == 0))
                    ok = false;
            }
            // Drop the probe reference so parent_phys2 returns to a lone owner.
            bigos::mm::frame_ref_dec_and_maybe_free(parent_phys2);
        }

        // Teardown both roots through the reference-counted path; ordered release
        // must free each frame exactly once and never early-free a shared frame.
        set_current_process_slot(nullptr);
        if (child.address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
            !bigos::mm::teardown_user_address_space(child.address_space_root))
            ok = false;
        close_all_fds(&child);
        free_fd_table(&child);
        if (child.kernel_stack_base != nullptr)
            bigos::free_pages(child.kernel_stack_base);

        if (parent.table_published)
            unpublish_process(&parent);
        if (parent.address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
            !bigos::mm::teardown_user_address_space(parent.address_space_root))
            ok = false;
        close_all_fds(&parent);
        free_fd_table(&parent);
        if (parent.kernel_stack_base != nullptr)
            bigos::free_pages(parent.kernel_stack_base);

        set_current_process_slot(saved_current);
        return ok;
    }

    void fork_cow_smoke_entry(void *) noexcept {
        if (fork_cow_smoke_run())
            bigos::serial_puts("BIGOS_FORK_COW_PASSED\n");
        else
            bigos::serial_puts("BIGOS_FORK_COW_FAILED\n");
    }
#endif

#ifdef BIGOS_TIME_IDENTITY_SMOKE
    // Default-off bounded validation of the wall-clock and identity primitives.
    // It covers, without entering ring3:
    //   1. wall clock advances monotonically over the tick (and stays >= the
    //      degradation baseline, so the RTC-invalid path remains monotonically
    //      usable),
    //   2. a non-fork-created process is root and fork's field-by-field identity
    //      inheritance is honored,
    //   3. the privilege decision allows root, allows an identity match, and
    //      rejects a non-match and null inputs.
    bool time_identity_smoke_run() noexcept {
        if (!g_proc_initialized)
            init();
        bool ok = true;

        // 1. Wall clock: baseline is at least the degradation floor, the current
        // time is at least the baseline, and consecutive reads never go backwards
        // (monotonic non-decreasing over the tick). Kept tight so the smoke runs
        // within a single scheduling quantum and does not race init's CR3.
        const int64_t boot = bigos::time::boot_unix_time();
        if (boot < bigos::time::RTC_INVALID_BASELINE)
            ok = false;
        int64_t prev = bigos::time::current_unix_time();
        if (prev < boot)
            ok = false;
        for (uint32_t i = 0; ok && i < 1000; i++) {
            const int64_t now = bigos::time::current_unix_time();
            if (now < prev)
                ok = false;
            prev = now;
        }

        // 2. Identity: a non-fork-created process is root; fork inheritance copies
        // the identity quad field-by-field.
        Process *saved_current = current_process_slot();
        static Process parent;
        static Process child;
        scrub_process(&parent);
        scrub_process(&child);

        if (ok && !create_first_user_process(&parent))
            ok = false;
        if (ok && (parent.uid != bigos::cred::ROOT_UID || parent.gid != bigos::cred::ROOT_UID ||
                      parent.euid != bigos::cred::ROOT_UID || parent.egid != bigos::cred::ROOT_UID))
            ok = false;

        // Give the parent a distinct non-root identity, then mirror fork's
        // inheritance contract into the child and confirm a field-by-field match.
        if (ok) {
            parent.uid = 1000;
            parent.gid = 1000;
            parent.euid = 1000;
            parent.egid = 1000;
            child.uid = parent.uid;
            child.gid = parent.gid;
            child.euid = parent.euid;
            child.egid = parent.egid;
            if (child.uid != parent.uid || child.gid != parent.gid || child.euid != parent.euid ||
                child.egid != parent.egid)
                ok = false;
        }

        // 3. Privilege decision: match allows, non-match rejects, root allows,
        // null rejects.
        if (ok) {
            static Process stranger;
            scrub_process(&stranger);
            stranger.uid = 2000;
            stranger.gid = 2000;
            stranger.euid = 2000;
            stranger.egid = 2000;

            static Process root_actor;
            scrub_process(&root_actor);   // all zero == root

            if (!bigos::cred::may_signal(&parent, &child))   // euid 1000 == target uid 1000
                ok = false;
            if (bigos::cred::may_signal(&stranger, &child))   // euid 2000 != 1000
                ok = false;
            if (!bigos::cred::may_signal(&root_actor, &stranger))   // root over anyone
                ok = false;
            if (bigos::cred::may_signal(nullptr, &child) || bigos::cred::may_signal(&parent, nullptr))
                ok = false;

            // File-access decision: root bypasses, owner read honored, other
            // write rejected for a 0644 file.
            if (!bigos::cred::permits(1000, 1000, 0644, bigos::cred::ROOT_UID, 0, bigos::cred::Access::Write))
                ok = false;
            if (!bigos::cred::permits(1000, 1000, 0644, 1000, 1000, bigos::cred::Access::Read))
                ok = false;
            if (bigos::cred::permits(1000, 1000, 0644, 3000, 3000, bigos::cred::Access::Write))
                ok = false;
        }

        // Teardown the parent created above through the reference-counted path.
        set_current_process_slot(nullptr);
        if (parent.table_published)
            unpublish_process(&parent);
        if (parent.address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
            !bigos::mm::teardown_user_address_space(parent.address_space_root))
            ok = false;
        close_all_fds(&parent);
        free_fd_table(&parent);
        if (parent.kernel_stack_base != nullptr)
            bigos::free_pages(parent.kernel_stack_base);

        set_current_process_slot(saved_current);
        return ok;
    }

    void time_identity_smoke_entry(void *) noexcept {
        if (time_identity_smoke_run())
            bigos::serial_puts("BIGOS_TIME_IDENTITY_PASSED\n");
        else
            bigos::serial_puts("BIGOS_TIME_IDENTITY_FAILED\n");
    }
#endif

#ifdef BIGOS_SIGNAL_SMOKE
    // Default-off bounded validation of the minimal signal model. It runs in
    // ordinary kernel context (no ring3 round-trip), exercising:
    //   1. fixed signal numbers / default actions / SIGKILL uncatchable +
    //      unblockable invariants,
    //   2. kill sets a pending bit; mask blocks then unblocks delivery,
    //   3. a user-handler delivery builds an on-user-stack signal frame and
    //      rewrites the interrupted frame, and sigreturn restores it exactly,
    //   4. an over-privileged kill is rejected by cred::may_signal.
    // The user-stack frame round-trip uses a real created user process address
    // space (signal helpers walk process->address_space_root directly), so it
    // does not need a CR3 switch. Any validation failure inside the delivery path
    // would terminate the current process, so the setup keeps every range valid.
    bool signal_smoke_run() noexcept {
        if (!g_proc_initialized)
            init();
        bool ok = true;

        namespace sig = bigos::signal;

        // 1. Static invariants.
        if (!sig::is_uncatchable(sig::SIGKILL) || !sig::is_unblockable(sig::SIGKILL))
            ok = false;
        if (sig::default_action(sig::SIGTERM) != sig::DefaultAction::Terminate)
            ok = false;
        if (sig::default_action(sig::SIGCHLD) != sig::DefaultAction::Ignore)
            ok = false;
        if (sig::valid_signo(0) || sig::valid_signo(sig::SIG_MAX + 1))
            ok = false;

        Process *saved_current = current_process_slot();
        static Process proc;
        scrub_process(&proc);

        if (ok && !create_first_user_process(&proc))
            ok = false;

        if (ok) {
            set_current_process_slot(&proc);
            proc.state = ProcessState::Running;

            // 2. kill sets pending; SIGKILL set_disposition rejected; mask drops
            // SIGKILL; mask blocks SIGUSR1 then unblock re-enables delivery.
            sig::init_state(&proc);
            if (sig::kill(&proc, 9999) != -bigos::EINVAL)
                ok = false;
            if (sig::kill(&proc, sig::SIGUSR1) != 0 || (proc.sig_pending & sig::signo_bit(sig::SIGUSR1)) == 0)
                ok = false;

            sig::SigDisposition handler_disp{sig::SigAction::Handler, USER_CODE_BASE};
            if (sig::set_disposition(&proc, sig::SIGKILL, &handler_disp, nullptr) != -bigos::EINVAL)
                ok = false;

            sig::SigSet old_mask = 0;
            if (sig::set_mask(
                    &proc, sig::SIG_BLOCK, sig::signo_bit(sig::SIGKILL) | sig::signo_bit(sig::SIGUSR1), &old_mask) != 0)
                ok = false;
            if ((proc.sig_mask & sig::signo_bit(sig::SIGKILL)) != 0)   // SIGKILL never blockable
                ok = false;
            if (sig::has_deliverable_signal(&proc))   // SIGUSR1 pending but blocked
                ok = false;
            if (sig::set_mask(&proc, sig::SIG_UNBLOCK, sig::signo_bit(sig::SIGUSR1), nullptr) != 0)
                ok = false;
            if (!sig::has_deliverable_signal(&proc))   // unblocked -> deliverable
                ok = false;

            // 3. User-handler delivery + sigreturn round-trip.
            if (ok) {
                sig::init_state(&proc);
                sig::SigDisposition disp{sig::SigAction::Handler, USER_CODE_BASE};
                if (sig::set_disposition(&proc, sig::SIGUSR1, &disp, nullptr) != 0)
                    ok = false;
                (void)sig::kill(&proc, sig::SIGUSR1);

                bigos::irq::InterruptFrame frame;
                memset(&frame, 0, sizeof(frame));
                bigos::arch::vm_user::force_user_return_frame(&frame, (1ull << 9) | (1ull << 1));   // IF + reserved
                frame.rip = USER_CODE_BASE + 0x40;
                frame.rsp = USER_STACK_TOP;
                frame.rax = 0x1111;
                frame.rbx = 0x2222;

                const uint64_t pre_rsp = frame.rsp;
                const uint64_t pre_rip = frame.rip;
                sig::deliver_pending_to_user(&frame, &proc);

                if (frame.rip != USER_CODE_BASE || frame.rdi != (uint64_t)sig::SIGUSR1 || frame.rsp >= pre_rsp)
                    ok = false;
                if ((proc.sig_mask & sig::signo_bit(sig::SIGUSR1)) == 0)   // blocked during handler
                    ok = false;
                if ((proc.sig_pending & sig::signo_bit(sig::SIGUSR1)) != 0)   // pending consumed
                    ok = false;

                sig::sigreturn(&frame);
                if (frame.rip != pre_rip || frame.rsp != pre_rsp || frame.rax != 0x1111 || frame.rbx != 0x2222)
                    ok = false;
                if (!bigos::arch::vm_user::is_user_return_frame(&frame))
                    ok = false;
                if ((frame.rflags & (1ull << 9)) == 0)   // IF preserved
                    ok = false;
                if ((proc.sig_mask & sig::signo_bit(sig::SIGUSR1)) != 0)   // mask restored
                    ok = false;
            }
        }

        // 4. Over-privileged kill is rejected by the cred decision (the SYS_KILL
        // enforcement primitive); a matching/root actor is allowed.
        if (ok) {
            static Process stranger;
            scrub_process(&stranger);
            stranger.euid = 4242;
            proc.uid = 7;
            proc.euid = 7;
            if (bigos::cred::may_signal(&stranger, &proc))
                ok = false;
            static Process root_actor;
            scrub_process(&root_actor);   // all zero == root
            if (!bigos::cred::may_signal(&root_actor, &proc))
                ok = false;
        }

        // Teardown the created process through the reference-counted path.
        set_current_process_slot(nullptr);
        if (proc.table_published)
            unpublish_process(&proc);
        if (proc.address_space_root != bigos::mm::INVALID_PHYS_ADDR &&
            !bigos::mm::teardown_user_address_space(proc.address_space_root))
            ok = false;
        close_all_fds(&proc);
        free_fd_table(&proc);
        if (proc.kernel_stack_base != nullptr)
            bigos::free_pages(proc.kernel_stack_base);

        set_current_process_slot(saved_current);
        return ok;
    }

    void signal_smoke_entry(void *) noexcept {
        if (signal_smoke_run())
            bigos::serial_puts("BIGOS_SIGNAL_PASSED\n");
        else
            bigos::serial_puts("BIGOS_SIGNAL_FAILED\n");
    }
#endif

#ifdef BIGOS_NONBLOCKING_FD_SMOKE
    // Validation-only, default-off bounded nonblocking fd (O_NONBLOCK) smoke. It
    // runs from ordinary kernel-thread context (can_block() true, non-IRQ) and
    // drives the fd-control surface (fcntl_fd_current F_GETFL/F_SETFL) over a
    // real growable fd table, asserting that:
    //   * F_SETFL toggles O_NONBLOCK and F_GETFL round-trips it together with the
    //     synthesized access mode, and F_SETFL(F_GETFL | O_NONBLOCK) (arg carries
    //     access-mode bits) still succeeds without -EINVAL and only changes the
    //     nonblocking bit,
    //   * a nonblocking empty-pipe read and a nonblocking full-pipe write return
    //     WouldBlock, while clearing the flag restores the blocking-capable path
    //     (verified by writing then reading the byte back),
    //   * poll_file readable <=> a nonblocking read does not WouldBlock,
    //   * a nonblocking tty read with no input returns WouldBlock, and terminal
    //     fd 0/1/2 sharing one open file description observe the nonblocking bit
    //     in lockstep through F_GETFL (linkage), and
    //   * a nonblocking unbound socket recv path returns -EAGAIN after a single
    //     bounded RX advance (covered indirectly via the recv-round selection).
    // It emits a deterministic BIGOS_NONBLOCKING_FD_PASSED / _FAILED marker.

    bool nbfd_pipe_check() noexcept {
        bigos::vfs::File *r = nullptr;
        bigos::vfs::File *w = nullptr;
        if (bigos::ipc::create(&r, &w) != bigos::vfs::Status::Success)
            return false;
        bool ok = true;

        const int64_t rfd = install_fd_current(r, false);
        const int64_t wfd = install_fd_current(w, false);
        if (rfd < 0 || wfd < 0) {
            bigos::vfs::release(r);
            bigos::vfs::release(w);
            return false;
        }

        // Default (blocking flag clear): F_GETFL reports the access mode with no
        // O_NONBLOCK bit. Read end is read-only, write end is write-only.
        if (ok && fcntl_fd_current((uint32_t)rfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                (int64_t)bigos::vfs::OPEN_RDONLY)
            ok = false;
        if (ok && fcntl_fd_current((uint32_t)wfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                (int64_t)bigos::vfs::OPEN_WRONLY)
            ok = false;

        // Set O_NONBLOCK on the read end and round-trip via F_GETFL.
        if (ok && fcntl_fd_current((uint32_t)rfd, bigos::proc::FCNTL_F_SETFL, bigos::proc::O_NONBLOCK) != 0)
            ok = false;
        if (ok && fcntl_fd_current((uint32_t)rfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                (int64_t)(bigos::vfs::OPEN_RDONLY | bigos::proc::O_NONBLOCK))
            ok = false;

        // poll says not readable on an empty pipe with the writer open, and a
        // nonblocking read returns WouldBlock (consistency contract).
        if (ok && (bigos::vfs::poll_file(r) & bigos::vfs::READY_READABLE) != 0)
            ok = false;
        char got = 0;
        size_t io = 0;
        if (ok && read_fd_current((uint32_t)rfd, &got, 1, &io) != bigos::vfs::Status::WouldBlock)
            ok = false;

        // Write one byte: poll now readable, nonblocking read succeeds.
        const char payload = 'N';
        io = 0;
        if (ok && write_fd_current((uint32_t)wfd, &payload, 1, &io) != bigos::vfs::Status::Success)
            ok = false;
        if (ok && (bigos::vfs::poll_file(r) & bigos::vfs::READY_READABLE) == 0)
            ok = false;
        io = 0;
        if (ok && (read_fd_current((uint32_t)rfd, &got, 1, &io) != bigos::vfs::Status::Success || io != 1 ||
                      got != payload))
            ok = false;

        // Clear O_NONBLOCK (F_SETFL with arg 0): F_GETFL drops the bit.
        if (ok && fcntl_fd_current((uint32_t)rfd, bigos::proc::FCNTL_F_SETFL, 0) != 0)
            ok = false;
        if (ok && fcntl_fd_current((uint32_t)rfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                (int64_t)bigos::vfs::OPEN_RDONLY)
            ok = false;

        // Standard idiom F_SETFL(F_GETFL | O_NONBLOCK) on the write end: arg
        // carries the access-mode bit (O_WRONLY) yet must succeed and only change
        // the nonblocking bit.
        if (ok) {
            const int64_t flags = fcntl_fd_current((uint32_t)wfd, bigos::proc::FCNTL_F_GETFL, 0);
            if (flags < 0 ||
                fcntl_fd_current((uint32_t)wfd, bigos::proc::FCNTL_F_SETFL,
                    (uint64_t)flags | bigos::proc::O_NONBLOCK) != 0)
                ok = false;
            if (ok && fcntl_fd_current((uint32_t)wfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                    (int64_t)(bigos::vfs::OPEN_WRONLY | bigos::proc::O_NONBLOCK))
                ok = false;
        }

        // Fill the pipe so the nonblocking write end returns WouldBlock.
        if (ok) {
            static uint8_t fill[bigos::ipc::PIPE_CAPACITY];
            for (uint32_t i = 0; i < bigos::ipc::PIPE_CAPACITY; i++)
                fill[i] = (uint8_t)i;
            size_t total = 0;
            while (ok && total < bigos::ipc::PIPE_CAPACITY) {
                size_t wrote = 0;
                const bigos::vfs::Status s =
                    write_fd_current((uint32_t)wfd, fill + total, bigos::ipc::PIPE_CAPACITY - total, &wrote);
                if (s != bigos::vfs::Status::Success || wrote == 0)
                    ok = false;
                total += wrote;
            }
            // Now full: poll not writable and a fresh nonblocking write would-blocks.
            if (ok && (bigos::vfs::poll_file(w) & bigos::vfs::READY_WRITABLE) != 0)
                ok = false;
            size_t wrote = 0;
            const char extra = 'X';
            if (ok && write_fd_current((uint32_t)wfd, &extra, 1, &wrote) != bigos::vfs::Status::WouldBlock)
                ok = false;
        }

        close_fd_current((uint32_t)rfd);
        close_fd_current((uint32_t)wfd);
        return ok;
    }

    bool nbfd_tty_check() noexcept {
        // Install fd 0/1/2 sharing one terminal open file description, matching
        // the real stdio layout, then assert nonblocking linkage across them.
        if (!install_standard_fds(current_process_slot()))
            return false;
        bool ok = true;

        // Set O_NONBLOCK on fd 1; fd 0 and fd 2 must observe the bit (shared OFD).
        if (ok && fcntl_fd_current(1, bigos::proc::FCNTL_F_SETFL, bigos::proc::O_NONBLOCK) != 0)
            ok = false;
        for (uint32_t fd = 0; ok && fd < 3; fd++) {
            const int64_t flags = fcntl_fd_current(fd, bigos::proc::FCNTL_F_GETFL, 0);
            if (flags < 0 || (uint64_t)(flags & (int64_t)bigos::proc::O_NONBLOCK) != bigos::proc::O_NONBLOCK)
                ok = false;
        }

        // With no input available, a nonblocking terminal read returns WouldBlock
        // and dequeues nothing (input ring untouched). fd 0 is the readable stdin.
        char got = 0;
        size_t io = 0;
        if (ok && read_fd_current(0, &got, 1, &io) != bigos::vfs::Status::WouldBlock)
            ok = false;

        // Clear O_NONBLOCK on fd 0; fd 1 and fd 2 must observe the cleared bit.
        if (ok && fcntl_fd_current(0, bigos::proc::FCNTL_F_SETFL, 0) != 0)
            ok = false;
        for (uint32_t fd = 0; ok && fd < 3; fd++) {
            const int64_t flags = fcntl_fd_current(fd, bigos::proc::FCNTL_F_GETFL, 0);
            if (flags < 0 || (flags & (int64_t)bigos::proc::O_NONBLOCK) != 0)
                ok = false;
        }

        close_fd_current(0);
        close_fd_current(1);
        close_fd_current(2);
        return ok;
    }

    // Fake network device backing the socket portion so it runs without a real
    // tap/network backend, mirroring the fd-readiness smoke's local device.
    struct NbfdSmokeDevice {
        bigos::device::NetworkDevice net;
        bigos::net::MacAddress mac;
    };
    NbfdSmokeDevice g_nbfd_dev = {};
    bigos::net::Context g_nbfd_ctx = {};

    bool nbfd_dev_ready(bigos::device::NetworkDevice *__d) noexcept { return __d != nullptr; }
    bool nbfd_dev_link_up(bigos::device::NetworkDevice *__d) noexcept { return __d != nullptr; }
    const uint8_t *nbfd_dev_mac(bigos::device::NetworkDevice *__d) noexcept {
        NbfdSmokeDevice *dev = __d == nullptr ? nullptr : (NbfdSmokeDevice *)__d->context;
        return dev == nullptr ? nullptr : dev->mac.bytes;
    }
    uint16_t nbfd_dev_mtu(bigos::device::NetworkDevice *) noexcept { return bigos::net::DEFAULT_MTU; }
    bigos::device::NetworkTxStatus nbfd_dev_transmit(
        bigos::device::NetworkDevice *, const void *, uint32_t, uint64_t) noexcept {
        return bigos::device::NetworkTxStatus::Success;
    }
    bigos::device::NetworkRxStatus nbfd_dev_poll_rx(
        bigos::device::NetworkDevice *, bigos::device::NetworkRxFrame *) noexcept {
        return bigos::device::NetworkRxStatus::NoFrame;
    }
    bigos::device::NetworkRxStatus nbfd_dev_return_rx(
        bigos::device::NetworkDevice *, const bigos::device::NetworkRxFrame *) noexcept {
        return bigos::device::NetworkRxStatus::Success;
    }
    void nbfd_dev_diagnostics(bigos::device::NetworkDevice *, bigos::device::NetworkDiagnostics *) noexcept {}

    bool nbfd_socket_check() noexcept {
        g_nbfd_dev = {};
        g_nbfd_ctx = {};
        g_nbfd_dev.mac = {{0x52, 0x54, 0x00, 0x21, 0x43, 0x65}};
        g_nbfd_dev.net.context = &g_nbfd_dev;
        g_nbfd_dev.net.ready = &nbfd_dev_ready;
        g_nbfd_dev.net.link_up = &nbfd_dev_link_up;
        g_nbfd_dev.net.mac = &nbfd_dev_mac;
        g_nbfd_dev.net.mtu = &nbfd_dev_mtu;
        g_nbfd_dev.net.transmit = &nbfd_dev_transmit;
        g_nbfd_dev.net.poll_rx = &nbfd_dev_poll_rx;
        g_nbfd_dev.net.return_rx = &nbfd_dev_return_rx;
        g_nbfd_dev.net.diagnostics = &nbfd_dev_diagnostics;

        bigos::net::Config config = {};
        config.local_ipv4 = bigos::net::make_ipv4(10, 0, 2, 15);
        config.netmask = bigos::net::make_ipv4(255, 255, 255, 0);
        config.direct_peer_ipv4 = bigos::net::make_ipv4(10, 0, 2, 2);
        config.local_mac = g_nbfd_dev.mac;
        config.mtu = bigos::net::DEFAULT_MTU;
        config.has_direct_peer = true;
        if (bigos::net::init(&g_nbfd_ctx, &g_nbfd_dev.net, &config) != bigos::net::Status::Ok)
            return false;

        bigos::vfs::File *file = nullptr;
        if (bigos::net::socket_create(&g_nbfd_ctx, &file) != bigos::vfs::Status::Success || file == nullptr)
            return false;
        const int64_t sfd = install_fd_current(file, false);
        if (sfd < 0) {
            bigos::vfs::release(file);
            return false;
        }
        bool ok = true;

        // A datagram socket is RDWR: F_GETFL reports the access mode, no O_NONBLOCK.
        if (ok && fcntl_fd_current((uint32_t)sfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                (int64_t)bigos::vfs::OPEN_RDWR)
            ok = false;
        // Toggle O_NONBLOCK and round-trip via F_GETFL on the shared open file
        // description; this is the flag the nonblocking recvfrom path consults.
        if (ok && fcntl_fd_current((uint32_t)sfd, bigos::proc::FCNTL_F_SETFL, bigos::proc::O_NONBLOCK) != 0)
            ok = false;
        if (ok && fcntl_fd_current((uint32_t)sfd, bigos::proc::FCNTL_F_GETFL, 0) !=
                (int64_t)(bigos::vfs::OPEN_RDWR | bigos::proc::O_NONBLOCK))
            ok = false;
        if (ok && !bigos::vfs::file_is_nonblocking(file))
            ok = false;

        // Bind a local port and confirm the empty rx queue reports not-readable;
        // this is the state where a nonblocking recvfrom returns -EAGAIN after a
        // single bounded RX advance (no poll-and-yield rounds).
        bigos::net::UdpEndpoint *endpoint = nullptr;
        if (ok && (bigos::net::udp_bind(&g_nbfd_ctx, 4200, &endpoint) != bigos::net::Status::Ok ||
                      endpoint == nullptr))
            ok = false;
        if (ok) {
            bigos::net::Socket *socket = bigos::net::socket_state(file);
            socket->endpoint = endpoint;
            socket->local_port = 4200;
            socket->bound = true;
            const uint32_t sp = bigos::vfs::poll_file(file);
            if ((sp & bigos::vfs::READY_READABLE) != 0 || (sp & bigos::vfs::READY_WRITABLE) == 0)
                ok = false;
        }

        close_fd_current((uint32_t)sfd);   // SOCKET_OPS.close recycles the endpoint
        return ok;
    }

    bool nonblocking_fd_smoke_run() noexcept {
        if (!g_proc_initialized)
            init();
        Process *saved_current = current_process_slot();

        static Process proc;
        scrub_process(&proc);
        proc.state = ProcessState::Running;
        init_fd_table(&proc);
        set_current_process_slot(&proc);

        bool ok = nbfd_pipe_check();
        if (ok)
            ok = nbfd_tty_check();
        if (ok)
            ok = nbfd_socket_check();

        close_all_fds(&proc);
        free_fd_table(&proc);
        set_current_process_slot(saved_current);
        return ok;
    }

    void nonblocking_fd_smoke_entry(void *) noexcept {
        if (nonblocking_fd_smoke_run())
            bigos::serial_puts("BIGOS_NONBLOCKING_FD_PASSED\n");
        else
            bigos::serial_puts("BIGOS_NONBLOCKING_FD_FAILED\n");
    }
#endif
}   // namespace bigos::proc
