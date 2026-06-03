import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_static_slab_size_classes_match_names() -> None:
    kmem = read_source('src/mm/kmem.cc')

    for size in (16, 32, 64, 128, 256, 512, 1024, 2048):
        assert f'static_slab({size}B, {size}, SLAB_PERMANENT);' in kmem
        assert f'static_cache({size}B, {size}, 0, 1, &static_slab_node_{size}B);' in kmem


def test_vmem_uses_9_bit_page_table_indexes() -> None:
    vmem = read_source('src/mm/vmem.cc')

    assert '#define INDEX_MASK 0x1fful' in vmem
    assert '#define get_pt_index(ADDR)   (((ADDR) >> INDEX_PT_OFFSET) & INDEX_MASK)' in vmem
    assert '0x00000000001f0000ul' not in vmem


def test_kvmem_region_is_not_described_as_direct_map() -> None:
    vmem = read_source('src/mm/vmem.cc')

    assert '#define KVMEM_BASE       0xffff880000000000ul' in vmem
    assert '#define KVMEM_LEN        0x10000000000ul' in vmem
    assert 'kernel heap/vmalloc-style virtual allocation area' in vmem
    assert 'not a direct map' in vmem
    assert 'not a virt = phys + offset region' in vmem


def test_page_count_and_physical_order_apis_are_separate() -> None:
    memory = read_source('include/bigos/memory.h')
    buddy = read_source('src/mm/buddy.h')
    slab = read_source('src/mm/slab.cc')
    vmem = read_source('src/mm/vmem.cc')

    assert 'alloc_kernel_pages(uint32_t __pages' in memory
    assert 'alloc_physical_order(uint32_t __order' in buddy
    assert 'alloc_kernel_pages(1u << buddy_order_, __gfm | _GFM_PRE_PAGING)' in slab
    assert 'alloc_physical_order(buddy_order, 0)' in vmem


def test_vmem_map_unmap_lifecycle_is_explicit() -> None:
    vmem = read_source('src/mm/vmem.cc')
    header = read_source('src/mm/vmem.h')

    assert 'bool VMem::map_kernel_range(MemoryBlock *__mblk) noexcept' in vmem
    assert 'void VMem::unmap_kernel_range(MemoryBlock *__mblk) noexcept' in vmem
    assert 'void VMem::rollback_kernel_range(uint64_t __base, uint32_t __nr_pages) noexcept' in vmem
    assert 'flush_kernel_tlb_page(vaddr);' in vmem
    assert 'asm volatile("invlpg (%0)"' in vmem
    assert 'kvmem.map_kernel_range(mblk)' in vmem
    assert 'set_paging(' not in header


def test_free_pages_unmaps_before_releasing_physical_backing() -> None:
    vmem = read_source('src/mm/vmem.cc')

    unmap_index = vmem.index('unmap_kernel_range(mblk);')
    release_index = vmem.index('release_physical_area(mblk);')
    free_physical_index = vmem.index('__detail::free_physical_order(physical_pair.first);')
    clear_area_index = vmem.index('while (!__mblk->physical_area.empty())')

    assert unmap_index < release_index
    assert free_physical_index < clear_area_index
    assert 'if (__mblk == nullptr || __mblk->physical_area.empty())' in vmem


def test_vmem_pre_paging_failure_rolls_back_mapping_and_backing() -> None:
    vmem = read_source('src/mm/vmem.cc')

    assert 'struct PagingDescriptorChange' in vmem
    assert 'clear_new_paging_descriptors(new_descriptors, new_descriptor_count);' in vmem
    assert 'new_descriptors[new_descriptor_count++]' in vmem
    assert 'uint32_t mapped_pages = 0;' in vmem
    assert 'rollback_kernel_range(__mblk->base, mapped_pages);' in vmem
    assert 'release_physical_area(mblk);' in vmem
    assert 'mm::__detail::free_physical_order(physical_addr);' in vmem
    assert 'kvmem.__free((void *)mblk->base);' in vmem
    assert 'mblk->physical_area.insert(node);' in vmem


def test_legacy_memory_api_aliases_are_not_exposed() -> None:
    for relative in (
        'include/bigos/memory.h',
        'src/mm/buddy.h',
        'src/mm/buddy.cc',
        'src/mm/kmem.h',
        'src/mm/vmem.cc',
    ):
        source = read_source(relative)
        assert re.search(r'(?<!_)alloc_pages\(', source) is None
        assert re.search(r'(?<!_)alloc_physical_pages\(', source) is None
        assert re.search(r'(?<!_)free_physical_pages\(', source) is None
        assert 'kmem_memory_alloc_pages' not in source


def test_kmalloc_callers_do_not_need_pre_paging_flag() -> None:
    kmem = read_source('src/mm/kmem.cc')
    new = read_source('cpp/libsupc++/new.cc')
    allocator = read_source('cpp/include/ktl/allocator.h')

    assert 'return kmem_cache.alloc(__size, __gfm);' in kmem
    assert '_GFM_PRE_PAGING' not in kmem
    assert 'return bigos::kmalloc(size);' in new
    assert '_GFM_PRE_PAGING' not in new
    assert 'bigos::kmalloc(sizeof(_Tp) * __n, __gfm)' in allocator
    assert '_GFM_PRE_PAGING' not in allocator


def test_buddy_split_metadata_failure_restores_original_block() -> None:
    buddy = read_source('src/mm/buddy.cc')

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
    for relative in ('src/mm/buddy.cc', 'src/mm/vmem.cc', 'src/mm/buddy.h', 'src/mm/vmem.h'):
        source = read_source(relative)
        assert 'debug_check_buddy' not in source
        assert 'debug_check_vmem' not in source


def test_fixed_memory_layout_constants_are_unchanged() -> None:
    buddy = read_source('src/mm/buddy.cc')
    vmem = read_source('src/mm/vmem.cc')
    link_script = read_source('link.lds')

    assert '#define KERNEL_BASE  0x1000000ul' in buddy
    assert '#define LOWEST_LIMIT 0x200000ul' in buddy
    assert '#define KERNEL_PML4_ADDR 0x2000ul' in vmem
    assert '. = 0xffffffff80000000;' in link_script
