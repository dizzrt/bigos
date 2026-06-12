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


def test_public_allocator_contracts_are_documented() -> None:
    memory = read_source('include/bigos/memory.h')
    new_cc = read_source('cpp/libsupc++/new.cc')

    for token in (
        'alloc_kernel_pages(uint32_t __pages',
        'free_pages(const void *__p)',
        'kmalloc(size_t __size',
        'free(const void *__p)',
    ):
        start = memory.index(token)
        context = memory[max(0, start - 180) : start + 160]
        assert 'Non-IRQ-handler-safe' in context

    assert 'Global new/delete are ordinary kmalloc/free frontends and are non-IRQ-handler-safe.' in new_cc
    assert 'return bigos::kmalloc(size);' in new_cc
    assert 'bigos::free(p);' in new_cc


def test_read_only_memory_diagnostics_have_context_contracts() -> None:
    memory = read_source('include/bigos/memory.h')
    buddy_h = read_source('kernel/mm/buddy.h')
    vmem_h = read_source('kernel/mm/vmem.h')
    slab_h = read_source('kernel/mm/slab.h')

    assert 'Context-agnostic after init: total number of physical pages is stable.' in buddy_h
    assert 'IRQ-disabled-only snapshot' in buddy_h
    assert 'Non-interrupt-context-only diagnostic output helper.' in buddy_h
    assert 'IRQ-disabled-only snapshot of the kernel heap/vmalloc free-page count.' in vmem_h
    assert 'IRQ-disabled-only boundary' in slab_h
    assert 'Non-interrupt-context-only' in memory


def test_interrupt_guard_preserves_if_state_and_documents_scope() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')

    guard_start = interrupt_h.index('class InterruptGuard')
    guard_body = interrupt_h[guard_start : interrupt_h.index('void initIRQ()', guard_start)]

    assert 'pushfq; popq %0' in guard_body
    assert 'RFLAGS_IF = 1ull << 9' in guard_body
    assert 'restore_enabled_' in guard_body
    assert 'asm volatile("cli"' in guard_body
    assert 'if (restore_enabled_)' in guard_body
    assert 'asm volatile("sti"' in guard_body
    assert 'same-CPU maskable IRQ interleaving only' in guard_body
    assert 'not an SMP lock, NMI guard, blocking primitive, or scheduler lock' in guard_body
    assert 'InterruptGuard(const InterruptGuard &) = delete;' in guard_body


def test_allocator_metadata_paths_use_interrupt_guard() -> None:
    buddy = read_source('kernel/mm/buddy.cc')
    slab = read_source('kernel/mm/slab.cc')
    vmem = read_source('kernel/mm/vmem.cc')

    for source in (buddy, slab, vmem):
        assert '#include <irq/interrupt.h>' in source
        assert 'bigos::irq::InterruptGuard guard;' in source

    assert 'void *alloc_physical_order(uint32_t __order, gfm_t __gfm) noexcept' in buddy
    assert 'void free_physical_order(const void *__p) noexcept' in buddy
    assert buddy.count('bigos::irq::InterruptGuard guard;') >= 3

    for token in (
        'account_large_alloc',
        'account_large_free',
        'Slab::alloc_obj',
        'Cache::free',
        'Cache::alloc',
        'CacheChain::collect_stats',
    ):
        assert token in slab
    assert slab.count('bigos::irq::InterruptGuard guard;') >= 8

    for token in (
        'rollback_kernel_range',
        'map_kernel_range',
        'physical_area.insert(node);',
        'VMem::__free',
        'VMem::__alloc_pages',
    ):
        assert token in vmem
    assert vmem.count('bigos::irq::InterruptGuard guard;') >= 7


def test_guarded_regions_do_not_introduce_blocking_tokens() -> None:
    for relative in ('kernel/mm/buddy.cc', 'kernel/mm/slab.cc', 'kernel/mm/vmem.cc'):
        source = read_source(relative)
        for token in ('mdelay', 'filesystem', 'scheduler', 'user-mode'):
            assert token not in source


def test_irq_handlers_avoid_ordinary_allocator_apis() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    timer_body = isr[isr.index('implement_isr(timer)') : isr.index('implement_isr(keyboard)')]
    keyboard_body = isr[isr.index('implement_isr(keyboard)') : isr.index('void init_isr_timer()')]
    page_fault_body = interrupt[
        interrupt.index('static bool page_fault_handler') : interrupt.index('static void default_external_irq_handler')
    ]

    forbidden = (
        'kmalloc',
        'alloc_kernel_pages',
        'free_pages',
        'new ',
        'delete',
        'mdelay',
    )
    for body in (timer_body, keyboard_body, page_fault_body):
        for token in forbidden:
            assert token not in body

    assert 'handle_keyboard_scancode(scancode);' in keyboard_body
    assert 'read_cr2()' in page_fault_body
    assert 'halt_cpu();' in page_fault_body
    assert 'map_kernel_range' not in page_fault_body
    assert 'retry' not in page_fault_body


def test_mm_self_test_stays_before_irq_enable_and_layout_aliases_are_unchanged() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    vmem = read_source('kernel/mm/vmem.cc')
    memory = read_source('include/bigos/memory.h')

    init_mem_index = kernel.index('bigos::init_mem(boot_info);')
    self_test_index = kernel.index('bigos::mm::self_test();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    assert init_mem_index < self_test_index < init_irq_index < enable_irq_index

    assert '#define KVMEM_BASE       0xffff880000000000ul' in vmem
    assert 'constexpr uintptr_t KDIRECT_BASE = 0xffff900000000000ul;' in memory
    assert 'alloc_pages(' not in memory
    assert 'alloc_physical_pages(' not in memory
    assert 'free_physical_pages(' not in memory
