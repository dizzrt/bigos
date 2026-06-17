from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/common.lua',
            ROOT / 'xmake/boot_artifacts.lua',
            ROOT / 'xmake/user_package.lua',
            ROOT / 'xmake/runtime.lua',
            ROOT / 'xmake/kernel.lua',
            ROOT / 'xmake/run_targets.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def test_process_header_declares_bounded_vma_model() -> None:
    proc_h = read_source('include/bigos/proc.h')

    for token in (
        'MAX_VMAS = 16',
        'USER_STACK_GUARD_PAGES',
        'USER_STACK_GROWTH_PAGES',
        'USER_HEAP_MAX_PAGES',
        'USER_ANON_BASE',
        'USER_RUNTIME_RESERVED_BASE',
        'struct UserRuntimeLayout',
        'UserRuntimeLayout runtime_layout;',
        'enum class VmaPermission',
        'enum class VmaPurpose',
        'enum class VmaBacking',
        'enum class VmaGrowth',
        'struct VmaEntry',
        'struct VmaCollection',
        'VmaCollection vmas;',
    ):
        assert token in proc_h


def test_vma_collection_rejects_overlap_overflow_kernel_half_wx_and_capacity() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    add_start = proc.index('bool internal_add_vma')
    add_body = proc[add_start : proc.index('bool internal_remove_vma', add_start)]

    assert 'vma_range_bounds(__entry.start, __entry.end)' in add_body
    assert '__end <= bigos::proc::USER_LOW_HALF_LIMIT' in proc
    assert 'permissions_wx(__entry.permissions)' in add_body
    assert '__vmas->count >= bigos::proc::MAX_VMAS' in add_body
    assert '__entry.start < existing.end && existing.start < __entry.end' in add_body


def test_user_mapping_is_vma_first_and_pte_attr_cannot_exceed_vma() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    map_start = proc.index('bool map_user_page_for_process')
    map_body = proc[map_start : proc.index('void unmap_and_free_user_page', map_start)]

    assert 'internal_find_vma(&__process->vmas, __vaddr)' in map_body
    assert 'internal_vma_range_allowed(&__process->vmas, __vaddr, PAGE_SIZE' in map_body
    assert 'internal_vma_attr_allowed(vma, __attr)' in map_body
    assert 'runtime_vma_allowed(__process, *vma)' in map_body
    assert 'map_page_in_root(__process->address_space_root, __vaddr, __phys, __attr)' in map_body
    assert '(__attr & bigos::mm::page_attr::WRITABLE) != 0' in proc
    assert '(__attr & bigos::mm::page_attr::NO_EXECUTE) == 0' in proc


def test_exec_and_first_process_publish_code_data_heap_stack_and_guard_vmas() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'internal_init_vmas(&__process->vmas);' in proc
    assert 'VmaPurpose::Code' in proc
    assert 'VmaPurpose::Data' in proc
    assert 'VmaPurpose::Heap' in proc
    assert 'VmaPurpose::StackGuard' in proc
    assert 'VmaPurpose::Stack' in proc
    assert 'init_runtime_layout(__process' in proc
    assert 'internal_add_process_vma(__process' in proc
    assert 'process->runtime_layout = prepared->runtime_layout;' in proc
    assert 'process->vmas = prepared->vmas;' in proc
    assert 'const UserRuntimeLayout old_runtime_layout = process->runtime_layout;' in proc
    assert 'const VmaCollection old_vmas = process->vmas;' in proc


def test_runtime_layout_rejects_reserved_gaps_and_mismatched_vma_purposes() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    layout_body = proc[proc.index('bool runtime_vma_allowed') : proc.index('void internal_init_vmas')]
    assert '!__process->runtime_layout.committed' in layout_body
    assert 'layout.future_runtime' in proc
    assert 'USER_RUNTIME_RESERVED_BASE' in proc
    assert 'USER_RUNTIME_RESERVED_END' in proc
    assert 'range_inside(__entry.start, __entry.end, layout.elf_load)' in layout_body
    assert 'range_inside(__entry.start, __entry.end, layout.heap)' in layout_body
    assert 'range_inside(__entry.start, __entry.end, layout.anonymous)' in layout_body
    assert 'range_inside_pair(__entry.start, __entry.end, layout.stack_growth, layout.stack)' in layout_body
    assert 'range_inside(__entry.start, __entry.end, layout.stack_guard)' in layout_body
    assert 'VmaBacking::Guard' in layout_body


def test_user_copy_validation_checks_vma_and_page_table_state() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    validate_start = proc.index('bool validate_user_buffer')
    validate_body = proc[validate_start : proc.index('int64_t install_fd_current', validate_start)]

    assert 'runtime_range_allowed(process, __addr, __len, VmaPermission::Read)' in validate_body
    assert 'bigos::mm::user_range_mapped(process->address_space_root, __addr, __len)' in validate_body
    assert 'runtime_range_allowed(process, __addr, __len, VmaPermission::Write)' in validate_body
    assert 'bigos::mm::__detail::user_range_writable(process->address_space_root, __addr, __len)' in validate_body
    assert 'copy_from_user_root(process->address_space_root' in validate_body
    assert 'copy_to_user_root(process->address_space_root' in validate_body


def test_brk_and_anonymous_mapping_are_restricted_and_lazy() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    syscall_h = read_source('include/bigos/syscall.h')
    syscall = read_source('kernel/core/syscall/syscall.cc')

    assert 'SYS_BRK = 8' in syscall_h
    assert 'SYS_MAP_ANON = 9' in syscall_h
    assert 'bigos::proc::brk_current(__new_break)' in syscall
    assert 'bigos::proc::map_anonymous_current(__len, __permissions, __flags)' in syscall
    assert 'result = __detail::sys_brk(__frame->rdi)' in syscall
    assert 'result = __detail::sys_map_anon(__frame->rdi, __frame->rsi, __frame->rdx)' in syscall

    brk_body = proc[proc.index('int64_t brk_current') : proc.index('int64_t map_anonymous_current')]
    assert '__new_break < process->vmas.heap_base' in brk_body
    assert '__new_break > process->vmas.heap_limit' in brk_body
    # Lazy growth: no eager per-page allocation in the grow branch.
    assert 'alloc_user_frame()' not in brk_body
    # Shrink unmaps only materialized pages, bounded by the high-water mark.
    assert 'heap->materialized_end < old_page_end ? heap->materialized_end : old_page_end' in brk_body
    assert 'unmap_and_free_user_page(process' in brk_body
    assert 'process->vmas.heap_break = __new_break' in brk_body

    anon_body = proc[proc.index('int64_t map_anonymous_current') : proc.index('bool try_handle_user_page_fault')]
    assert '__flags != 0' in anon_body
    assert '__len > USER_ANON_MAX_PAGES * PAGE_SIZE' in anon_body
    assert 'permissions_wx(permissions)' in anon_body
    assert (
        'const uint64_t anon_limit = process->runtime_layout.anonymous.base + process->runtime_layout.anonymous.len'
        in anon_body
    )
    assert 'VmaPurpose::Anonymous' in anon_body
    # Lazy backing: registration only, no eager allocation/mapping loop.
    assert 'alloc_user_frame()' not in anon_body
    assert 'process->vmas.anon_next = end' in anon_body


def test_anonymous_lifecycle_unmap_and_protect_are_bounded_and_staged() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    proc_h = read_source('include/bigos/proc.h')
    memory_h = read_source('include/bigos/memory.h')
    vmem = read_source('kernel/mm/vmem.cc')
    syscall_h = read_source('include/bigos/syscall.h')
    syscall = read_source('kernel/core/syscall/syscall.cc')
    user_sys = read_source('user/libc/syscall.c')
    user_mman = read_source('user/libc/include/sys/mman.h')
    smoke = read_source('user/smoke/anonymous_lifecycle_smoke.c')
    xmake = read_source('xmake.lua')

    assert 'int64_t unmap_anonymous_current(uint64_t __addr, uint64_t __len)' in proc_h
    assert 'int64_t protect_anonymous_current(uint64_t __addr, uint64_t __len, uint64_t __permissions)' in proc_h
    assert 'SYS_UNMAP_ANON = 36' in syscall_h
    assert 'SYS_PROTECT_ANON = 37' in syscall_h
    assert 'result = __detail::sys_unmap_anon(__frame->rdi, __frame->rsi)' in syscall
    assert 'result = __detail::sys_protect_anon(__frame->rdi, __frame->rsi, __frame->rdx)' in syscall

    assert 'lifecycle_range_bounds(__addr, __len, &end)' in proc
    assert 'stage_anonymous_unmap(process, __addr, end, &staged)' in proc
    assert 'stage_anonymous_protect(process, __addr, end, permissions, &staged)' in proc
    assert 'anonymous_lifecycle_vma_compatible' in proc
    assert '__entry->purpose == bigos::proc::VmaPurpose::Anonymous' in proc
    assert 'internal_remove_vma(__staged, entry->start, entry->end)' in proc
    assert 'clipped_vma(*entry, entry->start, cursor)' in proc
    assert 'clipped_vma(*entry, cut_end, entry->end)' in proc
    assert 'changed.permissions = __permissions' in proc
    assert 'process->vmas = staged' in proc

    assert 'unmap_user_page_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t *__phys)' in memory_h
    assert 'reclaim_empty_tables_in_root(root, __vaddr, PageTableOwner::UserAddressSpace, active)' in vmem
    assert 'frame_ref_dec_and_maybe_free(phys)' in proc
    assert 'remap_user_page_in_root(__process->address_space_root, page, phys, new_attr)' in proc
    assert 'PTE_COW' in proc

    assert 'int bigos_munmap_anon(void *addr, size_t len)' in user_mman
    assert 'int bigos_mprotect_anon(void *addr, size_t len, long prot)' in user_mman
    assert 'syscall2(SYS_UNMAP_ANON' in user_sys
    assert 'syscall3(SYS_PROTECT_ANON' in user_sys
    assert 'option("anonymous_lifecycle_smoke")' in xmake
    assert 'set_default(false)' in xmake[xmake.index('option("anonymous_lifecycle_smoke")'):]
    assert 'anonymous_lifecycle_smoke.c' in xmake
    assert 'BIGOS_ANON_LIFECYCLE_PASSED' in smoke
    assert 'access-after-unmap' in smoke
    assert 'write-after-readonly' in smoke


def test_unified_page_fault_entry_decides_by_vma_metadata() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    irq = read_source('kernel/core/irq/interrupt.cc')

    assert 'try_handle_user_page_fault(fault_address, error)' in irq
    assert 'bigos::proc::fault_current_and_exit(-14)' in irq

    entry_body = proc[proc.index('bool try_handle_user_page_fault') : proc.index('int64_t install_fd_current')]
    # Preconditions: running process, allocation-safe, present-bit gate. The
    # default userland page-fault path uses this allocation-safe entry:
    # a real ring3 #PF dispatches under the nonblocking guard with IF=0, so the
    # gate is the allocation-safe can_allocate_in_fault() rather than can_block().
    assert '!bigos::sched::can_allocate_in_fault()' in entry_body
    assert '(__error_code & 0x1) != 0' in entry_body
    # Locate the covering VMA and require anonymous backing.
    assert 'internal_find_vma_mut(&process->vmas, page)' in entry_body
    assert 'vma->backing != VmaBacking::Anonymous' in entry_body
    assert 'runtime_vma_allowed(process, *vma)' in entry_body
    assert 'runtime_vma_allowed(process, *cow_vma)' in entry_body
    # Permission and NX instruction-fetch gates.
    assert '(__error_code & 0x2) != 0 && !permissions_include(vma->permissions, VmaPermission::Write)' in entry_body
    assert (
        '(__error_code & (1ull << 4)) != 0 && !permissions_include(vma->permissions, VmaPermission::Execute)'
        in entry_body
    )
    # Downward stack branch reuses the shared materialization path.
    assert 'vma->growth == VmaGrowth::Down' in entry_body
    assert 'page >= vma->materialized_start' in entry_body
    assert 'vma->materialized_start = page' in entry_body
    # Heap range is bounded by the committed break.
    assert 'page_up(process->vmas.heap_break, &registered_end)' in entry_body
    # Shared anonymous attr keeps writable pages non-executable.
    assert 'anonymous_page_attr(vma->permissions)' in entry_body

    attr_body = proc[
        proc.index('bigos::mm::PageAttr anonymous_page_attr') : proc.index('bool map_user_page_for_process')
    ]
    assert 'page_attr::WRITABLE' in attr_body
    assert 'page_attr::NO_EXECUTE' in attr_body


def test_demand_paging_smoke_is_default_off_and_marker_emitting() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    xmake = read_source('xmake.lua')

    assert 'option("demand_paging_smoke")' in xmake
    assert 'set_default(false)' in xmake
    assert 'add_defines("BIGOS_DEMAND_PAGING_SMOKE")' in xmake

    assert '#ifdef BIGOS_DEMAND_PAGING_SMOKE' in proc
    assert 'BIGOS_DEMAND_PAGING_PASSED' in proc
    assert 'BIGOS_DEMAND_PAGING_FAILED' in proc
    # Allocation-failure injection is smoke-only, not a global test macro.
    assert 'g_demand_paging_force_alloc_fail' in proc


def test_low_level_boundaries_are_not_moved_or_widened() -> None:
    vmem = read_source('kernel/mm/vmem.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    assert '#define KVMEM_BASE       0xffff880000000000ul' in vmem
    assert '#define KERNEL_HIGHER_HALF_BASE 0xffffffff80000000ul' in vmem
    assert '#define SELF_MAPPING_BASE       0xffff800000000000ul' in vmem
    assert 'constexpr uint32_t HALF = 256;' in vmem
    assert 'entry[i_pt] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;' in vmem
    assert 'VECTOR_SYSCALL' in interrupt
    assert 'MUST NOT send an i8259 EOI' in interrupt
    assert 'driver::irqchip::i8259::send_eoi(irq_line);' in interrupt
