from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_page_table_ownership_metadata_and_publish_order() -> None:
    vmem = read_source('src/mm/vmem.cc')

    for token in (
        'enum class PageTableOwner',
        'KernelVmem',
        'UserAddressSpace',
        'enum class PageTableLevel',
        'struct PageTableMetadata',
        'uint16_t present_entries;',
        'PAGE_TABLE_METADATA_CAPACITY = 512',
    ):
        assert token in vmem

    ensure_start = vmem.index('static bool ensure_owned_descriptor')
    ensure_body = vmem[ensure_start : vmem.index('static bool ensure_root_descriptor', ensure_start)]
    register_index = ensure_body.index('register_page_table(__root_phys, page, __owner, __child_level)')
    publish_index = ensure_body.index('__entry[__index] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;')
    assert register_index < publish_index
    assert 'bigos::mm::__detail::free_physical_order((void *)page);' in ensure_body
    assert 'increment_present_entry(__parent_frame)' in ensure_body


def test_map_unmap_accounting_and_empty_table_reclaim() -> None:
    vmem = read_source('src/mm/vmem.cc')

    map_start = vmem.index('static bool map_single_page')
    map_body = vmem[map_start : vmem.index('static uint64_t *direct_table', map_start)]
    assert 'OwnedPagingDescriptorChange new_descriptors[3]' in map_body
    assert 'clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);' in map_body
    assert 'increment_present_entry(pt_phys)' in map_body
    assert 'paging_present(entry[i_pt])' in map_body

    unmap_start = vmem.index('void unmap_page(uint64_t __vaddr) noexcept')
    unmap_body = vmem[unmap_start : vmem.index('uint64_t derive_user_address_space_root', unmap_start)]
    assert '*pte = 0;' in unmap_body
    assert 'flush_kernel_tlb_page(__vaddr);' in unmap_body
    assert 'decrement_present_entry(pt_phys)' in unmap_body
    assert 'reclaim_active_empty_tables(root_phys, __vaddr, PageTableOwner::KernelVmem)' in unmap_body

    reclaim_body = vmem[vmem.index('static bool reclaim_active_empty_tables') : vmem.index('static bool teardown_user_low_half')]
    assert 'PageTableLevel::Pt' in reclaim_body
    assert 'PageTableLevel::Pd' in reclaim_body
    assert 'PageTableLevel::Pdpt' in reclaim_body
    assert 'unregister_page_table(pt_phys)' in reclaim_body
    assert 'unregister_page_table(pd_phys)' in reclaim_body
    assert 'unregister_page_table(pdpt_phys)' in reclaim_body


def test_user_teardown_preserves_borrowed_high_half_and_rejects_active_root() -> None:
    memory = read_source('include/bigos/memory.h')
    vmem = read_source('src/mm/vmem.cc')

    assert 'bool teardown_user_address_space(uint64_t __root_phys) noexcept;' in memory
    assert 'leaves copied kernel' in memory

    teardown_start = vmem.index('bool teardown_user_address_space(uint64_t __root_phys) noexcept')
    teardown_body = vmem[teardown_start : vmem.index('uint64_t read_cr3() noexcept', teardown_start)]
    assert '__root_phys == INVALID_PHYS_ADDR' in teardown_body
    assert 'root == (read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK)' in teardown_body
    assert 'PageTableLevel::Pml4' in teardown_body
    assert 'teardown_user_low_half(root)' in teardown_body
    assert '__detail::free_physical_order((void *)root);' in teardown_body

    low_half_body = vmem[vmem.index('static bool teardown_user_low_half') : teardown_start]
    assert 'USER_PML4_LIMIT = 256' in low_half_body
    assert 'PageTableOwner::UserAddressSpace' in low_half_body
    assert 'PAGING_DESCRIPTOR_LARGE_PAGE' in low_half_body
    assert 'free_physical_order((void *)leaf_phys)' in low_half_body


def test_process_exit_fault_and_reaper_are_safe_boundaries() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('src/kernel/proc/proc.cc')
    syscall = read_source('src/kernel/syscall/syscall.cc')
    sched = read_source('src/kernel/sched/sched.cc')
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    for token in (
        'bool reap_pending;',
        'bool resources_reclaimed;',
        'int64_t fault_reason;',
        'uint64_t kernel_stack_len;',
        'void reap_pending_processes() noexcept;',
    ):
        assert token in proc_h

    assert 'mark_reap_pending(process);' in proc
    assert 'BIGOS_USER_RECLAIMED' in proc
    assert 'BIGOS_USER_REAP_DEFERRED active-stack' in proc
    assert 'teardown_user_address_space(process->address_space_root)' in proc
    assert 'bigos::free_pages(process->kernel_stack_base);' in proc
    assert 'bigos::proc::fault_current_and_exit(SYS_EFAULT);' in syscall
    assert 'bigos::proc::fault_current_and_exit(-14);' in interrupt
    assert 'bigos::proc::reap_pending_processes();' in sched
