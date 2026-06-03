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


def test_page_count_and_physical_order_apis_are_separate() -> None:
    memory = read_source('include/bigos/memory.h')
    buddy = read_source('src/mm/buddy.h')
    slab = read_source('src/mm/slab.cc')
    vmem = read_source('src/mm/vmem.cc')

    assert 'alloc_kernel_pages(uint32_t __pages' in memory
    assert 'alloc_physical_order(uint32_t __order' in buddy
    assert 'alloc_kernel_pages(1u << buddy_order_, __gfm | _GFM_PRE_PAGING)' in slab
    assert 'alloc_physical_order(buddy_order, 0)' in vmem


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
