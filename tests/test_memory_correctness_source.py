import re
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


def test_static_slab_size_classes_match_names() -> None:
    kmem = read_source('kernel/mm/kmem.cc')
    memdef = read_source('kernel/mm/memdef.h')

    assert '#define CACHE_MAX_OBJ_SIZE 0x800ul' in memdef

    for size in (16, 32, 64, 128, 256, 512, 1024, 2048):
        slab_size = 'CACHE_MAX_OBJ_SIZE' if size == 2048 else str(size)
        assert f'static_slab({size}B, {slab_size}, SLAB_PERMANENT);' in kmem
        assert f'static_cache({size}B, {slab_size}, 0, 1, &static_slab_node_{size}B);' in kmem


def test_buddy_bootstrap_metadata_uses_internal_arena() -> None:
    buddy = read_source('kernel/mm/buddy.cc')
    header = read_source('kernel/mm/buddy.h')

    assert 'class EarlyMetadataArena' in buddy
    assert 'alignas(EARLY_METADATA_ALIGNMENT) static uint8_t gEarlyMetadataArenaStorage' in buddy
    assert 'EARLY_METADATA_MAX_BOOT_MEMORY_REGIONS = 128' in buddy
    assert 'EARLY_METADATA_REGION_SPLIT_BUDGET = 128' in buddy
    assert 'BIGOS_BOOT_INFO_V2_MAX_SIZE / sizeof(BootMemoryRegion)' in buddy
    assert 'new_bootstrap_page_block()' in buddy
    assert 'new_bootstrap_page_block_node(pblk)' in buddy
    assert 'early memory metadata arena exhausted while allocating %s' in buddy
    assert 'seal_early_metadata_arena();' in buddy
    assert 'EarlyMetadataArena' not in header


def test_buddy_runtime_split_stays_allocator_backed() -> None:
    buddy = read_source('kernel/mm/buddy.cc')

    split_start = buddy.index('ktl::intrusive_list_node<PageBlock *> *Zone::alloc')
    split_end = buddy.index('split_done:')
    split_body = buddy[split_start:split_end]

    assert 'auto temp_pblk = new PageBlock();' in split_body
    assert 'auto temp_node = new ktl::intrusive_list_node<PageBlock *>(temp_pblk);' in split_body
    assert 'new_bootstrap_page_block' not in split_body
    assert 'new_bootstrap_page_block_node' not in split_body


def test_vmem_uses_9_bit_page_table_indexes() -> None:
    vmem = read_source('kernel/mm/vmem.cc')

    assert '#define INDEX_MASK 0x1fful' in vmem
    assert '#define get_pt_index(ADDR)   (((ADDR) >> INDEX_PT_OFFSET) & INDEX_MASK)' in vmem
    assert '0x00000000001f0000ul' not in vmem


def test_kvmem_region_is_not_described_as_direct_map() -> None:
    vmem = read_source('kernel/mm/vmem.cc')

    assert '#define KVMEM_BASE       0xffff880000000000ul' in vmem
    assert '#define KVMEM_LEN        0x10000000000ul' in vmem
    assert 'kernel heap/vmalloc-style virtual allocation area' in vmem
    assert 'not a direct map' in vmem
    assert 'not a virt = phys + offset region' in vmem


def test_direct_map_window_is_independent_and_non_overlapping() -> None:
    memory = read_source('include/bigos/memory.h')
    vmem = read_source('kernel/mm/vmem.cc')
    docs = read_source('docs/en/arch/x86-boot-layout.md')

    assert 'constexpr uintptr_t KDIRECT_BASE = 0xffff900000000000ul;' in memory
    assert 'constexpr uintptr_t KDIRECT_LEN = 0x400000000000ul;' in memory
    assert '#define KVMEM_BASE       0xffff880000000000ul' in vmem
    assert '#define KVMEM_LEN        0x10000000000ul' in vmem
    assert '#define SELF_MAPPING_BASE       0xffff800000000000ul' in vmem
    assert '#define SELF_MAPPING_LEN        0x10000000000ul' in vmem
    assert '#define KERNEL_HIGHER_HALF_BASE 0xffffffff80000000ul' in vmem
    assert 'KDIRECT_BASE >= SELF_MAPPING_BASE + SELF_MAPPING_LEN' in vmem
    assert 'KDIRECT_BASE >= KVMEM_BASE + KVMEM_LEN' in vmem
    assert 'KDIRECT_BASE + bigos::mm::KDIRECT_LEN <= KERNEL_HIGHER_HALF_BASE' in vmem
    assert '0xffff900000000000..0xffffcfffffffffff  kernel direct map' in docs
    assert 'KVMEM heap/vmalloc-style allocation window, not direct map' in docs


def test_direct_map_helpers_are_checked_and_do_not_claim_kvmem_or_mmio() -> None:
    memory = read_source('include/bigos/memory.h')
    vmem = read_source('kernel/mm/vmem.cc')

    assert 'bool is_direct_mapped_phys(uint64_t __phys, uint64_t __len = 1) noexcept;' in memory
    assert 'void *phys_to_direct(uint64_t __phys) noexcept' in memory
    assert 'uint64_t direct_to_phys(const void *__addr) noexcept' in memory
    assert 'constexpr uint64_t INVALID_PHYS_ADDR = ~0ull;' in memory
    assert 'return nullptr;' in vmem[vmem.index('void *phys_to_direct') : vmem.index('uint64_t direct_to_phys')]
    assert 'return INVALID_PHYS_ADDR;' in vmem
    assert 'direct_map_memory_type_is_ram' in vmem
    assert '__type == BIGOS_BOOT_MEMORY_TYPE_USABLE' in vmem
    assert 'BIGOS_BOOT_MEMORY_TYPE_ACPI_RECLAIM' in vmem
    assert 'BIGOS_BOOT_MEMORY_TYPE_ACPI_NVS' in vmem
    assert 'BIGOS_BOOT_MEMORY_TYPE_BAD_MEMORY' in vmem
    assert 'phys_to_direct(bigos::mm::KDIRECT_LEN) != nullptr' in read_source('kernel/mm/self_test.cc')
    assert 'direct_to_phys((const void *)0xffff880000000000ul)' in read_source('kernel/mm/self_test.cc')


def test_page_count_and_physical_order_apis_are_separate() -> None:
    memory = read_source('include/bigos/memory.h')
    buddy = read_source('kernel/mm/buddy.h')
    slab = read_source('kernel/mm/slab.cc')
    vmem = read_source('kernel/mm/vmem.cc')

    assert 'alloc_kernel_pages(uint32_t __pages' in memory
    assert 'alloc_physical_order(uint32_t __order' in buddy
    assert 'alloc_kernel_pages(1u << buddy_order_, __gfm | _GFM_PRE_PAGING)' in slab
    assert 'alloc_physical_order(buddy_order, 0)' in vmem


def test_vmem_map_unmap_lifecycle_is_explicit() -> None:
    vmem = read_source('kernel/mm/vmem.cc')
    header = read_source('kernel/mm/vmem.h')

    assert 'bool VMem::map_kernel_range(MemoryBlock *__mblk) noexcept' in vmem
    assert 'void VMem::unmap_kernel_range(MemoryBlock *__mblk) noexcept' in vmem
    assert 'void VMem::rollback_kernel_range(uint64_t __base, uint32_t __nr_pages) noexcept' in vmem
    assert 'flush_kernel_tlb_page(vaddr);' in vmem
    assert 'asm volatile("invlpg (%0)"' in vmem
    assert 'kvmem.map_kernel_range(mblk)' in vmem
    assert 'set_paging(' not in header


def test_free_pages_unmaps_before_releasing_physical_backing() -> None:
    vmem = read_source('kernel/mm/vmem.cc')

    unmap_index = vmem.index('unmap_kernel_range(mblk);')
    release_index = vmem.index('release_physical_area(mblk);')
    free_physical_index = vmem.index('__detail::free_physical_order(physical_pair.first);')
    clear_area_index = vmem.index('while (!__mblk->physical_area.empty())')

    assert unmap_index < release_index
    assert free_physical_index < clear_area_index
    assert 'if (__mblk == nullptr || __mblk->physical_area.empty())' in vmem


def test_vmem_pre_paging_failure_rolls_back_mapping_and_backing() -> None:
    vmem = read_source('kernel/mm/vmem.cc')

    assert 'struct PagingDescriptorChange' in vmem
    assert 'clear_new_paging_descriptors(new_descriptors, new_descriptor_count);' in vmem
    assert '__changes[(*__change_count)++]' in vmem
    assert 'uint32_t mapped_pages = 0;' in vmem
    assert 'rollback_kernel_range(__mblk->base, mapped_pages);' in vmem
    assert 'release_physical_area(mblk);' in vmem
    assert 'mm::__detail::free_physical_order(physical_addr);' in vmem
    assert 'kvmem.__free((void *)mblk->base);' in vmem
    assert 'mblk->physical_area.insert(node);' in vmem


def test_direct_map_initialization_uses_bootinfo_ram_and_panics_on_partial_failure() -> None:
    vmem = read_source('kernel/mm/vmem.cc')
    kmem = read_source('kernel/mm/kmem.cc')

    assert 'void init_direct_map(const BootInfoHeader *__boot_info)' in vmem
    assert 'BootHandoff handoff = bigos_boot_resolve_handoff(__boot_info);' in vmem
    assert 'bigos_boot_info_v2_find_section(__header, BIGOS_BOOT_SECTION_TYPE_MEMORY_MAP)' in vmem
    assert (
        'init_direct_map_from_region(regions[i].physical_base, regions[i].length, regions[i].normalized_type);' in vmem
    )
    assert 'map_direct_page(bigos::mm::KDIRECT_BASE + phys, phys)' in vmem
    assert 'map_direct_large_page(bigos::mm::KDIRECT_BASE + phys, phys)' in vmem
    assert 'PAGING_DESCRIPTOR_LARGE_PAGE' in vmem
    assert 'DEFAULT_ATTR_PTE' in vmem
    assert 'DEFAULT_ATTR_PDE | PAGING_DESCRIPTOR_LARGE_PAGE' in vmem
    assert 'rollback_direct_map_range(__phys_base, mapped_pages);' in vmem
    assert 'PanicCode::DirectMapInitFailed' in vmem
    assert 'BIGOS_DIRECT_MAP_INIT_FAILED' in vmem

    buddy_index = kmem.index('__detail::init_buddy(__boot_info);')
    vmem_index = kmem.index('__detail::init_vmem();')
    direct_index = kmem.index('__detail::init_direct_map(__boot_info);')
    assert buddy_index < vmem_index < direct_index


def test_legacy_memory_api_aliases_are_not_exposed() -> None:
    for relative in (
        'include/bigos/memory.h',
        'kernel/mm/buddy.h',
        'kernel/mm/buddy.cc',
        'kernel/mm/kmem.h',
        'kernel/mm/vmem.cc',
    ):
        source = read_source(relative)
        assert re.search(r'(?<!_)alloc_pages\(', source) is None
        assert re.search(r'(?<!_)alloc_physical_pages\(', source) is None
        assert re.search(r'(?<!_)free_physical_pages\(', source) is None
        assert 'kmem_memory_alloc_pages' not in source


def test_kmalloc_callers_do_not_need_pre_paging_flag() -> None:
    kmem = read_source('kernel/mm/kmem.cc')
    new = read_source('cpp/libsupc++/new.cc')
    allocator = read_source('cpp/include/ktl/allocator.h')

    assert 'return kmem_cache.alloc(__size, __gfm);' in kmem
    assert 'if (__size > CACHE_MAX_OBJ_SIZE)' in kmem
    assert 'mm::alloc_large((uint32_t)__size, __gfm)' in kmem
    assert 'return bigos::kmalloc(size);' in new
    assert '_GFM_PRE_PAGING' not in new
    assert 'bigos::kmalloc(sizeof(_Tp) * __n, __gfm)' in allocator
    assert '_GFM_PRE_PAGING' not in allocator


def test_buddy_split_metadata_failure_restores_original_block() -> None:
    buddy = read_source('kernel/mm/buddy.cc')

    assert 'uint64_t original_base = pblk->base;' in buddy
    assert 'uint64_t original_len = pblk->len;' in buddy
    assert 'uint32_t original_order = pblk->order;' in buddy
    assert 'Zone *original_zone = pblk->zone;' in buddy
    assert 'split_failed:' in buddy
    assert 'pblk->base = original_base;' in buddy
    assert 'pblk->len = original_len;' in buddy
    assert 'pblk->order = original_order;' in buddy
    assert 'free_area_[original_order].insert(insert_position, pblk_node);' in buddy
    assert 'return nullptr;' in buddy
    assert buddy.index('return nullptr;') < buddy.index('gPageBlockList.insert(pblk_node);')
    assert buddy.index('gPageBlockList.insert(pblk_node);') < buddy.index('gNrFreePages -= pages;')


def test_no_debug_allocator_scanners_added() -> None:
    for relative in ('kernel/mm/buddy.cc', 'kernel/mm/vmem.cc', 'kernel/mm/buddy.h', 'kernel/mm/vmem.h'):
        source = read_source(relative)
        assert 'debug_check_buddy' not in source
        assert 'debug_check_vmem' not in source


def test_fixed_memory_layout_constants_are_unchanged() -> None:
    buddy = read_source('kernel/mm/buddy.cc')
    vmem = read_source('kernel/mm/vmem.cc')
    link_script = read_source('link.lds')

    assert '#define KERNEL_BASE  0x1000000ul' in buddy
    assert '#define LOWEST_LIMIT 0x200000ul' in buddy
    assert '#define KERNEL_PML4_ADDR 0x2000ul' in vmem
    assert '. = 0xffffffff80000000;' in link_script


def test_memory_self_test_is_switchable_and_not_default() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'option("mm_self_test")' in xmake
    assert 'option("slab_debug")' in xmake
    assert 'set_default(false)' in xmake
    assert 'add_defines("BIGOS_MM_SELF_TEST")' in xmake
    assert 'add_defines("BIGOS_SLAB_DEBUG")' in xmake
    assert '#ifdef BIGOS_MM_SELF_TEST\n    bigos::mm::self_test();\n#endif' in kernel

    init_mem_index = kernel.index('bigos::init_mem(boot_info);')
    self_test_index = kernel.index('bigos::mm::self_test();')
    irq_index = kernel.index('bigos::irq::initIRQ();')
    assert init_mem_index < self_test_index < irq_index


def test_memory_self_test_avoids_later_subsystem_dependencies() -> None:
    self_test = read_source('kernel/mm/self_test.cc')

    forbidden_tokens = (
        'initIRQ',
        'enableIRQ',
        'scheduler',
        'SMP',
        'filesystem',
        'fopen',
        'FILE *',
        'std::',
    )
    for token in forbidden_tokens:
        assert token not in self_test

    assert 'BIGOS_MM_SELF_TEST_PASSED' in self_test
    assert 'BIGOS_MM_SELF_TEST_FAILED stage=' in self_test
    assert 'alloc_kernel_pages(__pages, _GFM_PRE_PAGING)' in self_test
    assert 'alloc_physical_order(__order, 0)' in self_test
    assert 'run_direct_map_smoke();' in self_test
    assert 'is_direct_mapped_phys(phys, PAGE_SIZE)' in self_test
    assert 'phys_to_direct(phys)' in self_test
    assert 'direct_to_phys(direct) != phys' in self_test
    assert 'CACHE_MAX_OBJ_SIZE + 257' in self_test
    assert 'run_slab_reclaim_smoke();' in self_test
    assert 'kernel_vmem_free_pages()' in read_source('kernel/mm/vmem.h')


def test_large_allocation_header_and_free_dispatch_are_explicit() -> None:
    slab_h = read_source('kernel/mm/slab.h')
    slab = read_source('kernel/mm/slab.cc')
    kmem = read_source('kernel/mm/kmem.cc')

    assert 'SLAB_LARGE_ALLOC_MAGIC' in slab_h
    assert 'enum class AllocationKind' in slab_h
    assert 'LargePages = 2' in slab_h
    assert 'void *alloc_large(uint32_t __size, gfm_t __gfm) noexcept' in slab
    assert 'alloc_kernel_pages(pages, __gfm | _GFM_PRE_PAGING)' in slab
    assert 'SlabHeader(AllocationKind::LargePages, pages, __size, base)' in slab
    assert 'bool free_large(SlabHeader *__header, const void *__p) noexcept' in slab
    assert 'mm::free_large(slab_header, __p)' in kmem
    assert 'mm::was_recent_large_free(__p)' in kmem
    assert 'large allocation double free' in kmem
    assert 'free_pages(base);' in slab


def test_empty_dynamic_slab_reclaim_preserves_permanent_slabs() -> None:
    slab_h = read_source('kernel/mm/slab.h')
    slab = read_source('kernel/mm/slab.cc')

    assert 'bool should_reclaim_empty_slab(Slab *__slab) const noexcept' in slab_h
    assert '(__slab->flags_ & SLAB_PERMANENT)' in slab
    assert '__slab->nr_used_objs() != 0' in slab
    assert 'return avl_list.size() > 1;' in slab
    assert 'avl_list.erase(iter);' in slab
    assert 'bigos::free(bitmap);' in slab
    assert '__slab->~Slab();' in slab
    assert 'free_pages(heap);' in slab


def test_slab_stats_and_debug_guard_are_source_visible() -> None:
    memory = read_source('include/bigos/memory.h')
    slab_h = read_source('kernel/mm/slab.h')
    slab = read_source('kernel/mm/slab.cc')

    assert 'struct SlabCacheStats' in slab_h
    assert 'struct SlabAllocatorStats' in slab_h
    assert 'collect_slab_stats(SlabAllocatorStats *__stats)' in memory
    assert 'print_slab_stats()' in memory
    assert 'large_allocation_count' in slab_h
    assert 'large_allocation_pages' in slab_h
    assert 'BIGOS_SLAB_DEBUG' in slab
    assert 'slab object double free' in slab
    assert 'slab object invalid boundary' in slab
    assert 'SLAB_POISON_FREE' in slab


def test_perfect_fit_dynamic_cache_creation_is_disabled() -> None:
    memdef = read_source('kernel/mm/memdef.h')
    slab = read_source('kernel/mm/slab.cc')

    assert '#define GFM_PERFECT_FIT _GFM_PERFECT_FIT' in memdef
    assert 'Reserved for future kmem_cache_create-like work' in memdef
    assert 'Dynamic perfect-fit cache creation is intentionally unsupported.' in slab
    assert 'TODO new cache to fit' not in slab


def test_boot_debug_supports_bounded_serial_memory_smoke() -> None:
    boot_debug = read_source('tools/boot_debug.py')

    assert "MM_SELF_TEST_SUCCESS_MARKER = 'BIGOS_MM_SELF_TEST_PASSED'" in boot_debug
    assert "'--memory-self-test'" not in boot_debug
    assert "'xmake', 'build', 'kernel'" in boot_debug
    assert "'xmake', 'build', 'boot-artifacts'" in boot_debug
    assert "'--expect-serial-marker'" in boot_debug
    assert 'mode=file' in boot_debug
    assert 'launch_bochs_until_serial_marker' in boot_debug
