from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_page_attr_type_and_policy_constants_are_declared() -> None:
    memory = read_source('include/bigos/memory.h')

    assert 'using PageAttr = uint64_t;' in memory
    assert 'namespace page_attr {' in memory
    assert 'constexpr PageAttr PRESENT = 1ull << 0;' in memory
    assert 'constexpr PageAttr WRITABLE = 1ull << 1;' in memory
    assert 'constexpr PageAttr USER = 1ull << 2;' in memory
    assert 'constexpr PageAttr NO_EXECUTE = 1ull << 63;' in memory

    # Kernel default must equal supervisor present + writable (legacy 0x3).
    assert 'constexpr PageAttr KERNEL_DEFAULT = PRESENT | WRITABLE;' in memory
    # User data page: user + writable + NX.
    assert 'constexpr PageAttr USER_DATA = PRESENT | WRITABLE | USER | NO_EXECUTE;' in memory
    # User code page: user, NX cleared.
    assert 'constexpr PageAttr USER_CODE = PRESENT | USER;' in memory


def test_map_unmap_primitives_are_declared_with_context_contracts() -> None:
    memory = read_source('include/bigos/memory.h')

    assert 'bool map_page(uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept;' in memory
    assert 'void unmap_page(uint64_t __vaddr) noexcept;' in memory
    assert 'uint64_t derive_user_address_space_root() noexcept;' in memory

    map_index = memory.index('bool map_page(uint64_t __vaddr')
    context = memory[max(0, map_index - 800) : map_index]
    assert 'Non-interrupt-context-only' in context
    assert 'MUST NOT invoke' in context or 'MUST NOT' in context


def test_primitive_accepts_explicit_attr_and_guards_writes() -> None:
    vmem = read_source('src/mm/vmem.cc')

    # map primitive applies the explicit attribute on the leaf PTE.
    assert 'static bool map_single_page(uint64_t __vaddr, uint64_t __phys, uint64_t __attr) noexcept' in vmem
    assert 'entry[i_pt] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;' in vmem
    assert 'bool map_page(uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept' in vmem
    assert 'return map_single_page(__vaddr, __phys, __attr);' in vmem

    # User mappings propagate the user bit to intermediate levels.
    assert 'if ((__attr & bigos::mm::page_attr::USER) != 0)' in vmem
    assert 'intermediate_attr |= bigos::mm::page_attr::USER;' in vmem

    # Entry writes happen under an InterruptGuard.
    map_start = vmem.index('static bool map_single_page')
    map_end = vmem.index('static void rollback_direct_map_range', map_start)
    map_body = vmem[map_start:map_end]
    assert 'bigos::irq::InterruptGuard guard;' in map_body


def test_unmap_primitive_clears_pte_and_flushes_tlb() -> None:
    vmem = read_source('src/mm/vmem.cc')

    unmap_start = vmem.index('void unmap_page(uint64_t __vaddr) noexcept')
    unmap_body = vmem[unmap_start : unmap_start + 320]
    assert 'mapped_pte(__vaddr)' in unmap_body
    assert '*pte = 0;' in unmap_body
    assert 'flush_kernel_tlb_page(__vaddr);' in unmap_body
    assert 'bigos::irq::InterruptGuard guard;' in unmap_body


def test_kernel_range_uses_primitive_with_supervisor_default() -> None:
    vmem = read_source('src/mm/vmem.cc')

    map_start = vmem.index('bool VMem::map_kernel_range(MemoryBlock *__mblk) noexcept')
    map_end = vmem.index('void VMem::unmap_kernel_range', map_start)
    map_body = vmem[map_start:map_end]

    # Kernel range goes through the primitive with the supervisor default attr.
    assert 'map_single_page(base, physical_base, page_attr::KERNEL_DEFAULT)' in map_body
    # Failure path keeps existing rollback semantics.
    assert 'rollback_kernel_range(__mblk->base, mapped_pages);' in map_body
    # Kernel mapping must not set the user bit.
    assert 'page_attr::USER' not in map_body


def test_kernel_default_attr_is_bitwise_equivalent_to_legacy_pte() -> None:
    vmem = read_source('src/mm/vmem.cc')
    # The legacy supervisor present+writable encoding remains documented.
    assert '#define DEFAULT_ATTR_PTE   0x0000000000000003ul' in vmem


def test_user_root_copies_higher_half_and_zeros_lower_half() -> None:
    vmem = read_source('src/mm/vmem.cc')

    start = vmem.index('uint64_t derive_user_address_space_root() noexcept')
    end = vmem.index('VMem::rollback_kernel_range', start)
    body = vmem[start:end]

    assert 'constexpr uint32_t HALF = 256;' in body
    assert 'for (uint32_t i = 0; i < HALF; i++)' in body
    assert 'dst[i] = 0;' in body
    assert 'for (uint32_t i = HALF; i < 512; i++)' in body
    assert 'dst[i] = kernel_pml4[i];' in body
    # Resolves the active kernel PML4 via the self-mapping.
    assert 'self_mapping_pml4(0)' in body


def test_stage_does_not_switch_cr3_or_enter_ring3() -> None:
    vmem = read_source('src/mm/vmem.cc')
    memory = read_source('include/bigos/memory.h')

    # No CR3 write or ring3 transition instruction is introduced by this stage.
    for token in ('%%cr3', 'iretq', 'sysret', 'sysexit'):
        assert token not in vmem
    # The header records the non-goal explicitly.
    assert 'SHALL NOT write CR3' in memory or 'Does not write CR3' in memory


def test_user_vmem_smoke_marker_is_wired() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('src/kernel/kernel.cc')
    vmem = read_source('src/mm/vmem.cc')

    # Default-off build switch.
    assert 'option("user_vmem_smoke")' in xmake
    assert 'set_default(false)' in xmake
    assert 'add_defines("BIGOS_USER_VMEM_SMOKE")' in xmake

    # Wired into the kernel in non-interrupt context, before IRQ enable.
    smoke_index = kernel.index('bigos::mm::user_vmem_smoke();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    assert smoke_index < enable_irq_index

    # Deterministic markers and attribute checks.
    assert 'BIGOS_USER_VMEM_SMOKE_PASSED' in vmem
    assert 'BIGOS_USER_VMEM_SMOKE_FAILED' in vmem
    smoke_start = vmem.index('bool user_vmem_smoke() noexcept')
    smoke_body = vmem[smoke_start : vmem.index('void VMem::rollback_kernel_range', smoke_start)]
    assert 'page_attr::USER_DATA' in smoke_body
    assert '(*pte & page_attr::USER) != 0' in smoke_body
    assert '(*pte & page_attr::NO_EXECUTE) != 0' in smoke_body
    assert 'derive_user_address_space_root()' in smoke_body


def test_nxe_state_and_degradation_are_recorded() -> None:
    doc = read_source('docs/arch/user-address-space-vmem.md')
    boot = read_source('src/arch/x86/boot/boot.s')

    # boot.s enables LME but not NXE; doc must record the degradation.
    assert 'bts $0x08, %eax' in boot  # EFER.LME
    assert 'NXE' in doc
    assert '属性编码正确' in doc
