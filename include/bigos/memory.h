#ifndef _BIG_MEMORY_H
#define _BIG_MEMORY_H

#include <bigos/types.h>
#include <bigos/attributes.h>
#include <arch/x86/boot/boot_info.h>

NAMESPACE_BIGOS_BEG

// Allocates kernel virtual pages by page count, not by buddy order.
// Non-IRQ-handler-safe: ordinary callers must not invoke this from IRQ handlers.
_attr_nodiscard_ extern void *alloc_kernel_pages(uint32_t __pages, gfm_t __gfm) noexcept _attr_malloc_;

// Releases a kernel virtual range returned by alloc_kernel_pages().
// Non-IRQ-handler-safe: may update page tables and allocator metadata.
extern void free_pages(const void *__p) noexcept;

// Non-IRQ-handler-safe ordinary object allocator.
_attr_nodiscard_ extern void *kmalloc(size_t __size, gfm_t __gfm = 0) noexcept _attr_malloc_;

// Non-IRQ-handler-safe ordinary object/free path.
extern void free(const void *__p) noexcept;

void init_mem(const BootInfoHeader *__boot_info) noexcept;

namespace mm {
    constexpr uintptr_t KDIRECT_BASE = 0xffff900000000000ul;
    constexpr uintptr_t KDIRECT_LEN = 0x400000000000ul;
    constexpr uint64_t INVALID_PHYS_ADDR = ~0ull;

    struct SlabAllocatorStats;

    // Explicit page-table entry attributes for the map/unmap primitive.
    // Bit positions match x86_64 paging-structure entries so the value can be
    // OR-ed straight onto the physical frame address.
    using PageAttr = uint64_t;
    namespace page_attr {
        constexpr PageAttr PRESENT = 1ull << 0;
        constexpr PageAttr WRITABLE = 1ull << 1;
        constexpr PageAttr USER = 1ull << 2;
        constexpr PageAttr GLOBAL = 1ull << 8;
        constexpr PageAttr NO_EXECUTE = 1ull << 63;

        // Kernel default: present + writable, supervisor (user=0), executable
        // (NX=0). Bit-for-bit equivalent to the legacy DEFAULT_ATTR_PTE = 0x3.
        constexpr PageAttr KERNEL_DEFAULT = PRESENT | WRITABLE;
        // User data page: user-accessible, writable, non-executable.
        constexpr PageAttr USER_DATA = PRESENT | WRITABLE | USER | NO_EXECUTE;
        // User code page: user-accessible, executable (NX cleared).
        constexpr PageAttr USER_CODE = PRESENT | USER;
    }   // namespace page_attr

    // Non-interrupt-context-only page-table primitives. They write the active
    // kernel address space through the recursive self-mapping and mask same-CPU
    // maskable IRQ interleaving while updating entries; callers MUST NOT invoke
    // them from IRQ handlers.
    //
    // map_page() establishes a single 4 KiB mapping of __vaddr -> __phys using the
    // explicit __attr bits, allocating any missing intermediate page-table levels.
    // On allocation failure it rolls back the intermediate levels it created and
    // returns false. Intermediate levels inherit the user bit when __attr is a
    // user mapping so the leaf remains reachable from ring3 in a later change.
    _attr_nodiscard_ bool map_page(uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept;

    // unmap_page() clears the PTE for __vaddr (if present) and invalidates the TLB
    // entry via invlpg, matching the existing kernel unmap failure semantics.
    void unmap_page(uint64_t __vaddr) noexcept;

    // Derives a fresh user address-space page-table root from the current kernel
    // PML4: allocates one page, copies the higher-half top-level entries
    // (index 256..511, covering kernel higher-half, self-mapping, direct map and
    // KVMEM) and zeroes the lower-half entries (index 0..255). Returns the
    // physical address of the new root, or INVALID_PHYS_ADDR on failure.
    // This stage SHALL NOT write CR3, enter ring3 or load user code; self-mapping
    // in the derived root still resolves the kernel page tables.
    _attr_nodiscard_ uint64_t derive_user_address_space_root() noexcept;

    // Maps a single 4 KiB page into an explicit page-table root without changing
    // CR3. Used by the first user-program loader to populate a derived low-half
    // address space while the kernel root remains active.
    _attr_nodiscard_ bool map_page_in_root(
        uint64_t __root_phys, uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept;

    // Checks that a bounded range is below the canonical user half and backed by
    // present user PTEs in the supplied root. It does not fault in pages.
    _attr_nodiscard_ bool user_range_mapped(uint64_t __root_phys, uint64_t __vaddr, uint64_t __len) noexcept;

    _attr_nodiscard_ uint64_t read_cr3() noexcept;
    void activate_address_space_root(uint64_t __root_phys) noexcept;

    bool is_direct_mapped_phys(uint64_t __phys, uint64_t __len = 1) noexcept;
    _attr_nodiscard_ void *phys_to_direct(uint64_t __phys) noexcept;
    uint64_t direct_to_phys(const void *__addr) noexcept;

    // IRQ-disabled-only snapshot: internally masks same-CPU IRQ interleaving while reading allocator lists.
    extern void collect_slab_stats(SlabAllocatorStats *__stats) noexcept;
    // Non-interrupt-context-only diagnostic output helper.
    extern void print_slab_stats() noexcept;

    void self_test() noexcept;

#ifdef BIGOS_USER_VMEM_SMOKE
    // Validation-only, non-interrupt-context one-shot check of the page-attribute
    // primitives and user root derivation. Emits a deterministic BIGOS_ marker on
    // COM1 and returns true on success. Does not write CR3 or enter ring3.
    bool user_vmem_smoke() noexcept;
#endif
}   // namespace mm

NAMESPACE_BIGOS_END

#endif   // _BIG_MEMORY_H
