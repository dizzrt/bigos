#ifndef _BIG_MEMORY_H
#define _BIG_MEMORY_H

#include <bigos/types.h>
#include <bigos/attributes.h>

struct BootInfoHeader;

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

    struct MmContext {
        uint64_t root_phys;
        volatile uint32_t lock;
        volatile uint32_t refcount;
        volatile uint64_t active_cpu_mask;
        volatile uint64_t shootdown_generation;
        bool dying;
    };

    struct SlabAllocatorStats;

    enum class DeviceMmioCachePolicy : uint8_t {
        Uncached,
        WriteCombining,
    };

    struct DeviceMmioMapping {
        void *vaddr;
        uint64_t paddr;
        uint64_t length;
        DeviceMmioCachePolicy cache_policy;
        bool valid;
    };

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

        // Copy-on-write software marker. x86_64 leaf PTEs expose three
        // software-available bits (9..11) that the CPU ignores; COW uses bit 9.
        // It is orthogonal to the hardware present/writable/user/NX bits above:
        // a COW page is published present + user, with WRITABLE cleared and
        // PTE_COW set, so the next write faults into the COW split handler. This
        // is the single owner of bit 9; no other feature may reuse it.
        constexpr PageAttr PTE_COW = 1ull << 9;

        // Kernel default: present + writable, supervisor (user=0), executable
        // (NX=0). Bit-for-bit equivalent to the legacy DEFAULT_ATTR_PTE = 0x3.
        constexpr PageAttr KERNEL_DEFAULT = PRESENT | WRITABLE;
        // User data page: user-accessible, writable, non-executable.
        constexpr PageAttr USER_DATA = PRESENT | WRITABLE | USER | NO_EXECUTE;
        // User code page: user-accessible, executable (NX cleared).
        constexpr PageAttr USER_CODE = PRESENT | USER;
    }   // namespace page_attr

    enum class TlbInvalidationScope : uint8_t {
        Page,
        Range,
        AddressSpace,
    };

    enum class TlbInvalidationReason : uint8_t {
        Generic,
        Unmap,
        Protect,
        Cow,
        Teardown,
    };

    struct TlbInvalidationRequest {
        TlbInvalidationScope scope;
        TlbInvalidationReason reason;
        uint64_t address_space_root;
        uint64_t start_vaddr;
        uint64_t length;
        uint64_t target_cpu_mask;
        bool require_completion;
        MmContext *mm_context;
    };

    constexpr uint64_t TLB_TARGET_BOOTSTRAP_CPU = 1ull;

    _attr_nodiscard_ MmContext *create_mm_context(uint64_t __root_phys) noexcept;
    void retain_mm_context(MmContext *__context) noexcept;
    void release_mm_context(MmContext *__context) noexcept;
    void mark_mm_context_dying(MmContext *__context) noexcept;
    _attr_nodiscard_ uint64_t mm_context_root(const MmContext *__context) noexcept;
    _attr_nodiscard_ uint64_t mm_context_active_cpu_mask(const MmContext *__context) noexcept;
    _attr_nodiscard_ uint64_t mm_context_shootdown_generation(const MmContext *__context) noexcept;
    _attr_nodiscard_ bool enter_mm_context(MmContext *__context) noexcept;
    void leave_current_mm_context() noexcept;
    _attr_nodiscard_ uint64_t mm_context_target_mask(const MmContext *__context) noexcept;
    void handle_tlb_shootdown_ipi() noexcept;

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

    // Single-core-compatible TLB invalidation boundary. The current
    // implementation accepts only the bootstrap CPU target mask and completes by
    // local invlpg or CR3 reload; future SMP shootdown will extend this boundary
    // with remote target sets, IPI delivery and acknowledgement ordering.
    void invalidate_tlb(const TlbInvalidationRequest &__request) noexcept;

    // unmap_page() clears the PTE for __vaddr (if present) and invalidates the TLB
    // through the SMP-prepared local invalidation boundary.
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

    // Safe-context-only teardown for a derived user address-space root. It only
    // walks user low-half entries owned by the process, leaves copied kernel
    // high-half entries borrowed, refuses the active CR3 root, and releases the
    // PML4 root last.
    _attr_nodiscard_ bool teardown_user_address_space(uint64_t __root_phys) noexcept;

    // User physical-frame reference counting for copy-on-write sharing. The
    // table is a direct-map-resident fixed-size array indexed by physical frame
    // number, established once by init_frame_refcount() after init_direct_map().
    // Every leaf user frame returned by the user allocator is treated as having
    // an implicit initial count of one (single owner); fork sharing increments
    // it, and write-time split / address-space teardown decrement it. The frame
    // is returned to the buddy allocator only when the count reaches zero.
    //
    // Single-core / non-IRQ-context only: these mutate the table without atomics
    // and MUST NOT be called from IRQ handlers, matching the other mm
    // primitives. SMP would require per-frame locking or atomics plus TLB
    // shootdown coordination.
    void init_frame_refcount() noexcept;
    // Increments the reference count for a user frame. Returns false if the
    // count is already saturated (deterministic failure so fork can roll back)
    // or the frame is out of the table's range.
    _attr_nodiscard_ bool frame_ref_inc(uint64_t __phys) noexcept;
    // Decrements the reference count for a user frame and frees it to the buddy
    // allocator when the count reaches zero. Frames outside the table range (and
    // never-incremented frames, treated as a lone owner) are freed directly.
    void frame_ref_dec_and_maybe_free(uint64_t __phys) noexcept;
    // True when a user frame is currently shared by more than one owner. A
    // lone-owner or untracked frame reports false, so a write-time split can
    // restore write permission in place instead of copying.
    _attr_nodiscard_ bool frame_ref_is_shared(uint64_t __phys) noexcept;

    // Overwrites the leaf PTE for an existing user mapping in __root_phys with
    // __phys | __attr, invalidating the current page's TLB entry when __root is
    // the active CR3. The leaf PTE must already be present; this does not
    // allocate intermediate levels. Used by COW copy and write-time split.
    _attr_nodiscard_ bool remap_user_page_in_root(
        uint64_t __root_phys, uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept;

    // Clears one present user leaf in __root_phys, returns the former physical
    // frame through __phys, invalidates the current CPU when the root is active,
    // and reclaims empty dynamically owned user page-table pages. It does not
    // release the returned frame; process code applies frame-ref accounting.
    _attr_nodiscard_ bool unmap_user_page_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t *__phys) noexcept;

    // Reads the leaf physical frame and PTE attributes for a present user
    // mapping in __root_phys. Returns false when the page is not a present user
    // leaf. __phys and __attr may be null when only presence matters.
    _attr_nodiscard_ bool read_user_leaf_in_root(
        uint64_t __root_phys, uint64_t __vaddr, uint64_t *__phys, PageAttr *__attr) noexcept;

    _attr_nodiscard_ uint64_t read_cr3() noexcept;
    void activate_address_space_root(uint64_t __root_phys) noexcept;

    bool is_direct_mapped_phys(uint64_t __phys, uint64_t __len = 1) noexcept;
    _attr_nodiscard_ void *phys_to_direct(uint64_t __phys) noexcept;
    uint64_t direct_to_phys(const void *__addr) noexcept;

    // Device/MMIO ranges such as a firmware framebuffer must be mapped through
    // this explicit boundary before writes. Callers must not use the ordinary
    // RAM direct-map alias for framebuffer or other device memory.
    _attr_nodiscard_ DeviceMmioMapping map_device_mmio(
        uint64_t __phys, uint64_t __len, DeviceMmioCachePolicy __cache_policy) noexcept;

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
