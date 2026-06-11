from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_process_header_declares_bounded_vma_model() -> None:
    proc_h = read_source("include/bigos/proc.h")

    for token in (
        "MAX_VMAS = 16",
        "USER_STACK_GUARD_PAGES",
        "USER_STACK_GROWTH_PAGES",
        "USER_HEAP_MAX_PAGES",
        "USER_ANON_BASE",
        "enum class VmaPermission",
        "enum class VmaPurpose",
        "enum class VmaBacking",
        "enum class VmaGrowth",
        "struct VmaEntry",
        "struct VmaCollection",
        "VmaCollection vmas;",
    ):
        assert token in proc_h


def test_vma_collection_rejects_overlap_overflow_kernel_half_wx_and_capacity() -> None:
    proc = read_source("src/kernel/proc/proc.cc")

    add_start = proc.index("bool internal_add_vma")
    add_body = proc[add_start : proc.index("bool internal_remove_vma", add_start)]

    assert "vma_range_bounds(__entry.start, __entry.end)" in add_body
    assert "__end <= bigos::proc::USER_LOW_HALF_LIMIT" in proc
    assert "permissions_wx(__entry.permissions)" in add_body
    assert "__vmas->count >= bigos::proc::MAX_VMAS" in add_body
    assert "__entry.start < existing.end && existing.start < __entry.end" in add_body


def test_user_mapping_is_vma_first_and_pte_attr_cannot_exceed_vma() -> None:
    proc = read_source("src/kernel/proc/proc.cc")

    map_start = proc.index("bool map_user_page_for_process")
    map_body = proc[map_start : proc.index("void unmap_and_free_user_page", map_start)]

    assert "internal_find_vma(&__process->vmas, __vaddr)" in map_body
    assert "internal_vma_range_allowed(&__process->vmas, __vaddr, PAGE_SIZE" in map_body
    assert "internal_vma_attr_allowed(vma, __attr)" in map_body
    assert "map_page_in_root(__process->address_space_root, __vaddr, __phys, __attr)" in map_body
    assert "(__attr & bigos::mm::page_attr::WRITABLE) != 0" in proc
    assert "(__attr & bigos::mm::page_attr::NO_EXECUTE) == 0" in proc


def test_exec_and_first_process_publish_code_data_heap_stack_and_guard_vmas() -> None:
    proc = read_source("src/kernel/proc/proc.cc")

    assert "internal_init_vmas(&__process->vmas);" in proc
    assert "VmaPurpose::Code" in proc
    assert "VmaPurpose::Data" in proc
    assert "VmaPurpose::Heap" in proc
    assert "VmaPurpose::StackGuard" in proc
    assert "VmaPurpose::Stack" in proc
    assert "process->vmas = prepared.vmas;" in proc
    assert "const VmaCollection old_vmas = process->vmas;" in proc


def test_user_copy_validation_checks_vma_and_page_table_state() -> None:
    proc = read_source("src/kernel/proc/proc.cc")

    validate_start = proc.index("bool validate_user_buffer")
    validate_body = proc[validate_start : proc.index("int64_t install_fd_current", validate_start)]

    assert "internal_vma_range_allowed(&process->vmas, __addr, __len, VmaPermission::Read)" in validate_body
    assert "bigos::mm::user_range_mapped(process->address_space_root, __addr, __len)" in validate_body
    assert "internal_vma_range_allowed(&process->vmas, __addr, __len, VmaPermission::Write)" in validate_body
    assert "bigos::mm::__detail::user_range_writable(process->address_space_root, __addr, __len)" in validate_body
    assert "copy_from_user_root(process->address_space_root" in validate_body
    assert "copy_to_user_root(process->address_space_root" in validate_body


def test_brk_and_anonymous_mapping_are_restricted_and_lazy() -> None:
    proc = read_source("src/kernel/proc/proc.cc")
    syscall_h = read_source("include/bigos/syscall.h")
    syscall = read_source("src/kernel/syscall/syscall.cc")

    assert "SYS_BRK = 8" in syscall_h
    assert "SYS_MAP_ANON = 9" in syscall_h
    assert "bigos::proc::brk_current(__new_break)" in syscall
    assert "bigos::proc::map_anonymous_current(__len, __permissions, __flags)" in syscall
    assert "result = __detail::sys_brk(__frame->rdi)" in syscall
    assert "result = __detail::sys_map_anon(__frame->rdi, __frame->rsi, __frame->rdx)" in syscall

    brk_body = proc[proc.index("int64_t brk_current") : proc.index("int64_t map_anonymous_current")]
    assert "__new_break < process->vmas.heap_base" in brk_body
    assert "__new_break > process->vmas.heap_limit" in brk_body
    # Lazy growth: no eager per-page allocation in the grow branch.
    assert "alloc_user_frame()" not in brk_body
    # Shrink unmaps only materialized pages, bounded by the high-water mark.
    assert "heap->materialized_end < old_page_end ? heap->materialized_end : old_page_end" in brk_body
    assert "unmap_and_free_user_page(process" in brk_body
    assert "process->vmas.heap_break = __new_break" in brk_body

    anon_body = proc[proc.index("int64_t map_anonymous_current") : proc.index("bool try_handle_user_page_fault")]
    assert "__flags != 0" in anon_body
    assert "__len > USER_ANON_MAX_PAGES * PAGE_SIZE" in anon_body
    assert "permissions_wx(permissions)" in anon_body
    assert "VmaPurpose::Anonymous" in anon_body
    # Lazy backing: registration only, no eager allocation/mapping loop.
    assert "alloc_user_frame()" not in anon_body
    assert "process->vmas.anon_next = end" in anon_body


def test_unified_page_fault_entry_decides_by_vma_metadata() -> None:
    proc = read_source("src/kernel/proc/proc.cc")
    irq = read_source("src/kernel/irq/interrupt.cc")

    assert "try_handle_user_page_fault(fault_address, error)" in irq
    assert "bigos::proc::fault_current_and_exit(-14)" in irq

    entry_body = proc[proc.index("bool try_handle_user_page_fault") : proc.index("int64_t install_fd_current")]
    # Preconditions: running process, allocation-safe, present-bit gate. Stage 19:
    # a real ring3 #PF dispatches under the nonblocking guard with IF=0, so the
    # gate is the allocation-safe can_allocate_in_fault() rather than can_block().
    assert "!bigos::sched::can_allocate_in_fault()" in entry_body
    assert "(__error_code & 0x1) != 0" in entry_body
    # Locate the covering VMA and require anonymous backing.
    assert "internal_find_vma_mut(&process->vmas, page)" in entry_body
    assert "vma->backing != VmaBacking::Anonymous" in entry_body
    # Permission and NX instruction-fetch gates.
    assert "(__error_code & 0x2) != 0 && !permissions_include(vma->permissions, VmaPermission::Write)" in entry_body
    assert "(__error_code & (1ull << 4)) != 0 && !permissions_include(vma->permissions, VmaPermission::Execute)" in entry_body
    # Downward stack branch reuses the shared materialization path.
    assert "vma->growth == VmaGrowth::Down" in entry_body
    assert "page >= vma->materialized_start" in entry_body
    assert "vma->materialized_start = page" in entry_body
    # Heap range is bounded by the committed break.
    assert "page_up(process->vmas.heap_break, &registered_end)" in entry_body
    # Shared anonymous attr keeps writable pages non-executable.
    assert "anonymous_page_attr(vma->permissions)" in entry_body

    attr_body = proc[proc.index("bigos::mm::PageAttr anonymous_page_attr") : proc.index("bool map_user_page_for_process")]
    assert "page_attr::WRITABLE" in attr_body
    assert "page_attr::NO_EXECUTE" in attr_body


def test_demand_paging_smoke_is_default_off_and_marker_emitting() -> None:
    proc = read_source("src/kernel/proc/proc.cc")
    xmake = read_source("xmake.lua")

    assert 'option("demand_paging_smoke")' in xmake
    assert "set_default(false)" in xmake
    assert 'add_defines("BIGOS_DEMAND_PAGING_SMOKE")' in xmake

    assert "#ifdef BIGOS_DEMAND_PAGING_SMOKE" in proc
    assert "BIGOS_DEMAND_PAGING_PASSED" in proc
    assert "BIGOS_DEMAND_PAGING_FAILED" in proc
    # Allocation-failure injection is smoke-only, not a global test macro.
    assert "g_demand_paging_force_alloc_fail" in proc


def test_low_level_boundaries_are_not_moved_or_widened() -> None:
    vmem = read_source("src/mm/vmem.cc")
    interrupt = read_source("src/kernel/irq/interrupt.cc")

    assert "#define KVMEM_BASE       0xffff880000000000ul" in vmem
    assert "#define KERNEL_HIGHER_HALF_BASE 0xffffffff80000000ul" in vmem
    assert "#define SELF_MAPPING_BASE       0xffff800000000000ul" in vmem
    assert "constexpr uint32_t HALF = 256;" in vmem
    assert "entry[i_pt] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;" in vmem
    assert "VECTOR_SYSCALL" in interrupt
    assert "MUST NOT send an i8259 EOI" in interrupt
    assert "driver::irqchip::i8259::send_eoi(irq_line);" in interrupt
