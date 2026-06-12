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


def test_pte_cow_marker_is_centralized_and_unique() -> None:
    memory = read_source('include/bigos/memory.h')

    # Single owner of leaf PTE bit 9, documented orthogonal to the hardware bits.
    assert 'constexpr PageAttr PTE_COW = 1ull << 9;' in memory
    assert 'single owner of bit 9' in memory
    # Exactly one definition of the COW marker constant.
    assert memory.count('PTE_COW = 1ull << 9') == 1


def test_frame_refcount_table_declared_and_initialized_after_direct_map() -> None:
    memory = read_source('include/bigos/memory.h')
    vmem = read_source('kernel/mm/vmem.cc')
    kmem = read_source('kernel/mm/kmem.cc')

    for token in (
        'void init_frame_refcount() noexcept;',
        'bool frame_ref_inc(uint64_t __phys) noexcept;',
        'void frame_ref_dec_and_maybe_free(uint64_t __phys) noexcept;',
        'bool frame_ref_is_shared(uint64_t __phys) noexcept;',
        'non-IRQ-context only',
    ):
        assert token in memory

    # The table lives in vmem.cc and is a frame-number-indexed array.
    assert 'static uint16_t *gFrameRefcount;' in vmem
    assert 'void init_frame_refcount() noexcept' in vmem
    # Saturation is a deterministic failure (no wrap-around).
    inc_body = vmem[vmem.index('bool frame_ref_inc(') : vmem.index('void frame_ref_dec_and_maybe_free(')]
    assert 'FRAME_REFCOUNT_MAX' in inc_body
    assert 'return false' in inc_body

    # init_frame_refcount runs in init_mem after init_direct_map.
    init_body = kmem[kmem.index('void init_mem(') : kmem.index('}   // namespace mm')]
    assert init_body.index('init_direct_map(__boot_info)') < init_body.index('init_frame_refcount()')


def test_teardown_uses_reference_counted_release() -> None:
    vmem = read_source('kernel/mm/vmem.cc')

    low_half = vmem[
        vmem.index('static bool teardown_user_low_half') : vmem.index('static void rollback_direct_map_range')
    ]
    # Leaf frames go through the reference-count-aware release, not a raw free.
    assert 'bigos::mm::frame_ref_dec_and_maybe_free(leaf_phys);' in low_half


def test_remap_and_leaf_accessor_present() -> None:
    memory = read_source('include/bigos/memory.h')
    vmem = read_source('kernel/mm/vmem.cc')

    assert 'bool remap_user_page_in_root(' in memory
    assert 'bool read_user_leaf_in_root(' in memory

    remap_body = vmem[vmem.index('bool remap_user_page_in_root(') : vmem.index('uint64_t read_cr3() noexcept')]
    # Overwrites the present leaf PTE and invalidates the TLB on the active root.
    assert 'flush_kernel_tlb_page(__vaddr);' in remap_body
    assert 'read_cr3()' in remap_body


def test_sys_fork_number_and_dispatch_branch() -> None:
    syscall_h = read_source('include/bigos/syscall.h')
    syscall = read_source('kernel/core/syscall/syscall.cc')

    # SYS_FORK is appended right after SYS_MAP_ANON = 9.
    assert 'SYS_MAP_ANON = 9' in syscall_h
    assert 'SYS_FORK = 10' in syscall_h

    # Dispatch routes to fork_current with the parent frame and no EOI changes.
    dispatch_body = syscall[syscall.index('void dispatch(') :]
    assert 'case SYS_FORK:' in dispatch_body
    assert 'bigos::proc::fork_current(__frame)' in dispatch_body
    assert '__frame->rax = (uint64_t)result;' in dispatch_body


def test_fork_clone_and_cow_split_implemented() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'int64_t fork_current(const bigos::irq::InterruptFrame *__parent_frame) noexcept;' in proc_h
    assert 'bool fork_entry_valid;' in proc_h

    # Address-space clone derives a child root, mirrors the kernel stack, and
    # value-copies the VMA collection.
    clone_body = proc[proc.index('bool clone_user_address_space_cow(') : proc.index('bool clone_fd_table(')]
    assert 'derive_user_address_space_root()' in clone_body
    assert 'clone_process_kernel_stack_mapping(__child)' in clone_body
    assert '__child->vmas = __parent->vmas;' in clone_body

    # Per-page split: ELF segments copied, writable anon shared COW, read-only
    # anon shared directly.
    leaf_body = proc[proc.index('bool clone_user_leaf(') : proc.index('bool clone_user_address_space_cow(')]
    assert 'VmaBacking::ElfSegment' in leaf_body
    assert 'cow_shared_attr(__parent_attr)' in leaf_body
    assert 'frame_ref_inc(__parent_phys)' in leaf_body
    assert 'remap_user_page_in_root(' in leaf_body

    # fd table copy retains the shared vfs::File per descriptor.
    fd_body = proc[proc.index('bool clone_fd_table(') : proc.index('void fork_child_entry(')]
    assert 'bigos::vfs::retain(entries[i].file)' in fd_body
    assert 'MAX_FDS_SOFT_LIMIT' in fd_body

    # COW write-split branch in the unified page-fault handler.
    split_body = proc[
        proc.index(
            'bool cow_split_current(Process *__process, uint64_t __page, const VmaEntry *__vma) noexcept {'
        ) : proc.index('int64_t fork_current(')
    ]
    assert 'frame_ref_is_shared(old_phys)' in split_body
    assert 'frame_ref_dec_and_maybe_free(old_phys)' in split_body

    fault_body = proc[
        proc.index('bool try_handle_user_page_fault(') : proc.index(
            'bool cow_split_current(Process *__process, uint64_t __page, const VmaEntry *__vma) noexcept {'
        )
    ]
    assert 'page_attr::PTE_COW' in fault_body
    assert 'cow_split_current(process, page, cow_vma)' in fault_body


def test_fork_failure_rolls_back_and_returns_negative_errno() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    fork_body = proc[proc.index('int64_t fork_current(') : proc.index('int64_t install_fd_current(')]
    # Deterministic negative errno on each failure path; no panic.
    assert '-bigos::ENOMEM' in fork_body
    assert '-bigos::EAGAIN' in fork_body
    # Soft-limit guard.
    assert 'MAX_PROCESSES_SOFT_LIMIT' in fork_body
    # Rollback releases child root, fd table, kernel stack, and process object.
    assert 'teardown_user_address_space(child->address_space_root)' in fork_body
    assert 'free_fd_table(child)' in fork_body
    assert 'bigos::free_pages(child->kernel_stack_base)' in fork_body
    assert 'free_process_object(child)' in fork_body
    # Parent gets child PID on success.
    assert 'return (int64_t)child_pid;' in fork_body


def test_fork_cow_smoke_switch_and_marker_present() -> None:
    xmake = read_source('xmake.lua')
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'option("fork_cow_smoke")' in xmake
    assert 'add_defines("BIGOS_FORK_COW_SMOKE")' in xmake
    # Existing smoke matrix is preserved.
    assert 'option("demand_paging_smoke")' in xmake
    assert 'option("growable_tables_smoke")' in xmake

    assert '#ifdef BIGOS_FORK_COW_SMOKE' in proc_h
    assert 'void fork_cow_smoke_entry(void *) noexcept;' in proc_h

    assert 'BIGOS_FORK_COW_PASSED' in proc
    assert 'BIGOS_FORK_COW_FAILED' in proc
    assert 'fork_cow_smoke_entry' in kernel
