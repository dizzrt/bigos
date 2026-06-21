#include <string.h>
#include <arch/x86/boot/boot_info.h>
#include <bigos/io.h>   //TODO remove later
#include <bigos/panic.h>
#include <bigos/percpu.h>

#include "vmem.h"
#include "buddy.h"
#include "memdef.h"
#include <bigos/memory.h>
#include <bigos/smp_ipi.h>
#include <irq/interrupt.h>

#define KVMEM_LEN        0x10000000000ul
#define KVMEM_BASE       0xffff880000000000ul
#define KERNEL_PML4_ADDR 0x2000ul
#define KERNEL_HIGHER_HALF_BASE 0xffffffff80000000ul
#define SELF_MAPPING_BASE       0xffff800000000000ul
#define SELF_MAPPING_LEN        0x10000000000ul

#define DEFAULT_ATTR_PML4E 0x0000000000000003ul
#define DEFAULT_ATTR_PDPTE 0x0000000000000003ul
#define DEFAULT_ATTR_PDE   0x0000000000000003ul
#define DEFAULT_ATTR_PTE   0x0000000000000003ul
#define PAGING_DESCRIPTOR_LARGE_PAGE 0x80ul

#define INDEX_PML4_OFFSET 39
#define INDEX_PDPT_OFFSET 30
#define INDEX_PD_OFFSET   21
#define INDEX_PT_OFFSET   12

#define INDEX_MASK 0x1fful

#define get_pml4_index(ADDR) (((ADDR) >> INDEX_PML4_OFFSET) & INDEX_MASK)
#define get_pdpt_index(ADDR) (((ADDR) >> INDEX_PDPT_OFFSET) & INDEX_MASK)
#define get_pd_index(ADDR)   (((ADDR) >> INDEX_PD_OFFSET) & INDEX_MASK)
#define get_pt_index(ADDR)   (((ADDR) >> INDEX_PT_OFFSET) & INDEX_MASK)

#define self_mapping_pml4(ADDR) ((uint64_t *)0xffff804020100000ul)
#define self_mapping_pdpt(ADDR) ((uint64_t *)((get_pml4_index(ADDR) << 12) | 0xffff804020000000ul))
#define self_mapping_pd(ADDR)                                                                                          \
    ((uint64_t *)((get_pml4_index(ADDR) << 21) | (get_pdpt_index(ADDR) << 12) | 0xffff804000000000ul))
#define self_mapping_pt(ADDR)                                                                                          \
    ((uint64_t *)((get_pml4_index(ADDR) << 30) | (get_pdpt_index(ADDR) << 21) | (get_pd_index(ADDR) << 12) |           \
                  0xffff800000000000ul))

#define PAGING_DESCRIPTOR_ADDR_MASK 0x000ffffffffff000ul

static bigos::mm::VMem kvmem;

struct TlbShootdownSlot {
    volatile uint32_t lock;
    volatile bool active;
    bigos::mm::TlbInvalidationScope scope;
    uint64_t address_space_root;
    uint64_t start_vaddr;
    uint64_t length;
    uint64_t target_cpu_mask;
    volatile uint64_t ack_cpu_mask;
    uint64_t generation;
};

static TlbShootdownSlot g_tlb_shootdown_slot = {};
static constexpr uint32_t TLB_SHOOTDOWN_TIMEOUT_ITERATIONS = 1000000;

static_assert(bigos::mm::KDIRECT_BASE >= SELF_MAPPING_BASE + SELF_MAPPING_LEN);
static_assert(bigos::mm::KDIRECT_BASE >= KVMEM_BASE + KVMEM_LEN);
static_assert(bigos::mm::KDIRECT_BASE + bigos::mm::KDIRECT_LEN <= KERNEL_HIGHER_HALF_BASE);
static_assert(KVMEM_BASE < bigos::mm::KDIRECT_BASE || KVMEM_BASE >= bigos::mm::KDIRECT_BASE + bigos::mm::KDIRECT_LEN);

static inline bool paging_present(uint64_t __descriptor) noexcept {
    return (__descriptor & 0x1ul) != 0;
}

static inline void local_invlpg(uint64_t __vaddr) noexcept {
    asm volatile("invlpg (%0)" ::"r"(__vaddr) : "memory");
}

static inline void reload_local_cr3() noexcept {
    uint64_t cr3;
    asm volatile("movq %%cr3, %0" : "=r"(cr3));
    asm volatile("movq %0, %%cr3" ::"r"(cr3) : "memory");
}

static inline void invalidate_active_tlb_page(uint64_t __root_phys, uint64_t __vaddr) noexcept {
    bigos::mm::TlbInvalidationRequest request = {
        bigos::mm::TlbInvalidationScope::Page,
        bigos::mm::TlbInvalidationReason::Generic,
        __root_phys & PAGING_DESCRIPTOR_ADDR_MASK,
        __vaddr,
        PAGE_SIZE,
        bigos::mm::TLB_TARGET_BOOTSTRAP_CPU,
        true,
    };
    bigos::mm::invalidate_tlb(request);
}

struct PagingDescriptorChange {
    uint64_t *entry;
    uint16_t index;
};

enum class PageTableOwner : uint8_t {
    None = 0,
    KernelVmem,
    UserAddressSpace,
};

enum class PageTableLevel : uint8_t {
    Pml4 = 4,
    Pdpt = 3,
    Pd = 2,
    Pt = 1,
};

struct PageTableMetadata {
    bool used;
    PageTableOwner owner;
    PageTableLevel level;
    uint64_t root_phys;
    uint64_t frame_phys;
    uint16_t present_entries;
};

struct OwnedPagingDescriptorChange {
    uint64_t *entry;
    uint16_t index;
    uint64_t parent_frame;
    uint64_t child_frame;
    uint64_t root_phys;
    PageTableOwner owner;
    PageTableLevel child_level;
};

struct DirectMapRange {
    uint64_t base;
    uint64_t len;
};

constexpr uint32_t DIRECT_MAP_MAX_RANGES = 128;
static DirectMapRange gDirectMapRanges[DIRECT_MAP_MAX_RANGES];
static uint32_t gDirectMapRangeCount;

// User physical-frame reference count table (copy-on-write). Indexed by
// physical frame number (phys >> PAGE_SHIFT). The storage is a buddy-allocated
// contiguous block accessed through the direct map; it lives outside the slab
// and offers O(1) frame-number indexing. A zero entry means "untracked"; the
// implicit owner count of a freshly allocated user frame is one. fork sharing
// increments, write-time split / teardown decrement; the frame returns to the
// buddy allocator only when the count drops back to zero. Single-core /
// non-IRQ-context only: mutated without atomics, never from an IRQ handler.
static uint16_t *gFrameRefcount;
static uint64_t gFrameRefcountMaxFrame;

constexpr uint16_t FRAME_REFCOUNT_MAX = 0xffffu;

constexpr uint32_t PAGE_TABLE_METADATA_CAPACITY = 512;
static PageTableMetadata gPageTableMetadata[PAGE_TABLE_METADATA_CAPACITY];

static void clear_new_paging_descriptors(PagingDescriptorChange *__changes, uint32_t __count) noexcept {
    while (__count) {
        __count--;
        __changes[__count].entry[__changes[__count].index] = 0;
    }
}

static PageTableMetadata *find_page_table_metadata(uint64_t __frame) noexcept {
    const uint64_t frame = __frame & PAGING_DESCRIPTOR_ADDR_MASK;
    for (uint32_t i = 0; i < PAGE_TABLE_METADATA_CAPACITY; i++) {
        if (gPageTableMetadata[i].used && gPageTableMetadata[i].frame_phys == frame)
            return &gPageTableMetadata[i];
    }
    return nullptr;
}

static bool register_page_table(
    uint64_t __root, uint64_t __frame, PageTableOwner __owner, PageTableLevel __level) noexcept {
    const uint64_t frame = __frame & PAGING_DESCRIPTOR_ADDR_MASK;
    if (find_page_table_metadata(frame) != nullptr)
        return false;

    for (uint32_t i = 0; i < PAGE_TABLE_METADATA_CAPACITY; i++) {
        if (!gPageTableMetadata[i].used) {
            gPageTableMetadata[i] = {true, __owner, __level, __root & PAGING_DESCRIPTOR_ADDR_MASK, frame, 0};
            return true;
        }
    }
    return false;
}

static void unregister_page_table(uint64_t __frame) noexcept {
    PageTableMetadata *metadata = find_page_table_metadata(__frame);
    if (metadata != nullptr)
        metadata->used = false;
}

static bool page_table_owned_by(
    uint64_t __frame, uint64_t __root, PageTableOwner __owner, PageTableLevel __level) noexcept {
    PageTableMetadata *metadata = find_page_table_metadata(__frame);
    return metadata != nullptr && metadata->owner == __owner && metadata->level == __level &&
           metadata->root_phys == (__root & PAGING_DESCRIPTOR_ADDR_MASK);
}

static bool increment_present_entry(uint64_t __frame) noexcept {
    PageTableMetadata *metadata = find_page_table_metadata(__frame);
    if (metadata == nullptr)
        return true;   // Static/borrowed page tables are intentionally not reclaimable.
    if (metadata->present_entries == INDEX_MASK + 1)
        return false;
    metadata->present_entries++;
    return true;
}

static bool decrement_present_entry(uint64_t __frame) noexcept {
    PageTableMetadata *metadata = find_page_table_metadata(__frame);
    if (metadata == nullptr)
        return true;
    if (metadata->present_entries == 0)
        return false;
    metadata->present_entries--;
    return true;
}

static bool page_table_empty(uint64_t __frame) noexcept {
    PageTableMetadata *metadata = find_page_table_metadata(__frame);
    return metadata != nullptr && metadata->present_entries == 0;
}

static void clear_owned_paging_descriptors(OwnedPagingDescriptorChange *__changes, uint32_t __count) noexcept {
    while (__count) {
        __count--;
        OwnedPagingDescriptorChange &change = __changes[__count];
        change.entry[change.index] = 0;
        decrement_present_entry(change.parent_frame);
        unregister_page_table(change.child_frame);
        bigos::mm::__detail::free_physical_order((void *)change.child_frame);
    }
}

static uint64_t *mapped_pte(uint64_t __vaddr) noexcept {
    uint64_t *entry = self_mapping_pml4(__vaddr);
    if (!paging_present(entry[get_pml4_index(__vaddr)]))
        return nullptr;

    entry = self_mapping_pdpt(__vaddr);
    if (!paging_present(entry[get_pdpt_index(__vaddr)]))
        return nullptr;

    entry = self_mapping_pd(__vaddr);
    if (!paging_present(entry[get_pd_index(__vaddr)]))
        return nullptr;

    return &self_mapping_pt(__vaddr)[get_pt_index(__vaddr)];
}

static uint64_t align_up_page(uint64_t __value) noexcept {
    return (__value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static uint64_t align_down_page(uint64_t __value) noexcept {
    return __value & ~(PAGE_SIZE - 1);
}

static void shootdown_lock(volatile uint32_t *__lock) noexcept {
    while (__atomic_exchange_n(__lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        while (__atomic_load_n(__lock, __ATOMIC_RELAXED) != 0u)
            asm volatile("pause" ::: "memory");
    }
}

static void shootdown_unlock(volatile uint32_t *__lock) noexcept {
    __atomic_store_n(__lock, 0u, __ATOMIC_RELEASE);
}

static void perform_local_tlb_invalidation(
    bigos::mm::TlbInvalidationScope __scope, uint64_t __start_vaddr, uint64_t __length) noexcept {
    if (__scope == bigos::mm::TlbInvalidationScope::AddressSpace) {
        reload_local_cr3();
        return;
    }

    uint64_t len = __length == 0 ? PAGE_SIZE : __length;
    uint64_t vaddr = align_down_page(__start_vaddr);
    const uint64_t end = align_up_page(__start_vaddr + len);
    while (vaddr < end) {
        local_invlpg(vaddr);
        if (__scope == bigos::mm::TlbInvalidationScope::Page)
            break;
        vaddr += PAGE_SIZE;
    }
}

static bool wait_for_tlb_shootdown_ack(uint64_t __target_mask) noexcept {
    for (uint32_t i = 0; i < TLB_SHOOTDOWN_TIMEOUT_ITERATIONS; i++) {
        const uint64_t ack = __atomic_load_n(&g_tlb_shootdown_slot.ack_cpu_mask, __ATOMIC_ACQUIRE);
        if ((ack & __target_mask) == __target_mask)
            return true;
        asm volatile("pause" ::: "memory");
    }
    return false;
}

static void direct_map_panic(const char *__message) noexcept {
    bigos::serial_init();
    bigos::kpanic(bigos::PanicCode::DirectMapInitFailed, "mm-direct-map", "%s\n", __message);
}

static bool direct_map_select_range(
    uint64_t __base, uint64_t __len, uint64_t *__out_base, uint64_t *__out_len) noexcept {
    if (__len == 0 || __base >= bigos::mm::KDIRECT_LEN)
        return false;

    uint64_t end = __base + __len;
    if (end <= __base)
        end = bigos::mm::KDIRECT_LEN;
    if (end > bigos::mm::KDIRECT_LEN)
        end = bigos::mm::KDIRECT_LEN;

    uint64_t aligned_base = align_up_page(__base);
    uint64_t aligned_end = align_down_page(end);
    if (aligned_end <= aligned_base)
        return false;

    *__out_base = aligned_base;
    *__out_len = aligned_end - aligned_base;
    return true;
}

static bool direct_map_range_covered(uint64_t __phys, uint64_t __len) noexcept {
    if (__len == 0 || __phys >= bigos::mm::KDIRECT_LEN)
        return false;

    uint64_t end = __phys + __len;
    if (end <= __phys || end > bigos::mm::KDIRECT_LEN)
        return false;

    for (uint32_t i = 0; i < gDirectMapRangeCount; i++) {
        uint64_t range_end = gDirectMapRanges[i].base + gDirectMapRanges[i].len;
        if (__phys >= gDirectMapRanges[i].base && end <= range_end)
            return true;
    }
    return false;
}

static bool direct_map_record_range(uint64_t __base, uint64_t __len) noexcept {
    if (gDirectMapRangeCount >= DIRECT_MAP_MAX_RANGES)
        return false;
    gDirectMapRanges[gDirectMapRangeCount++] = {__base, __len};
    return true;
}

static bool ensure_paging_descriptor(uint64_t *__entry, uint16_t __index, uint64_t __attr,
    PagingDescriptorChange *__changes, uint32_t *__change_count) noexcept {
    if (paging_present(__entry[__index])) {
        if ((__attr & bigos::mm::page_attr::USER) != 0 && (__entry[__index] & bigos::mm::page_attr::USER) == 0) {
            bigos::irq::InterruptGuard guard;
            __entry[__index] |= bigos::mm::page_attr::USER;
        }
        return true;
    }

    uint64_t page = (uint64_t)bigos::mm::__detail::alloc_physical_order(0, 0);
    if (page == 0)
        return false;

    __changes[(*__change_count)++] = {__entry, __index};
    {
        // Mask same-CPU maskable IRQ interleaving while publishing the entry.
        bigos::irq::InterruptGuard guard;
        __entry[__index] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
    }
    return true;
}

static bool map_direct_page(uint64_t __vaddr, uint64_t __phys) noexcept {
    uint16_t i_pml4 = get_pml4_index(__vaddr);
    uint16_t i_pdpt = get_pdpt_index(__vaddr);
    uint16_t i_pd = get_pd_index(__vaddr);
    uint16_t i_pt = get_pt_index(__vaddr);
    PagingDescriptorChange new_descriptors[3] = {};
    uint32_t new_descriptor_count = 0;

    uint64_t *entry = self_mapping_pml4(__vaddr);
    if (!ensure_paging_descriptor(entry, i_pml4, DEFAULT_ATTR_PML4E, new_descriptors, &new_descriptor_count)) {
        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    entry = self_mapping_pdpt(__vaddr);
    if (new_descriptor_count != 0 && new_descriptors[new_descriptor_count - 1].entry == self_mapping_pml4(__vaddr))
        memset((void *)entry, 0, PAGE_SIZE);

    if (!ensure_paging_descriptor(entry, i_pdpt, DEFAULT_ATTR_PDPTE, new_descriptors, &new_descriptor_count)) {
        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    entry = self_mapping_pd(__vaddr);
    if (new_descriptor_count != 0 && new_descriptors[new_descriptor_count - 1].entry == self_mapping_pdpt(__vaddr))
        memset((void *)entry, 0, PAGE_SIZE);

    if (!ensure_paging_descriptor(entry, i_pd, DEFAULT_ATTR_PDE, new_descriptors, &new_descriptor_count)) {
        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    entry = self_mapping_pt(__vaddr);
    if (new_descriptor_count != 0 && new_descriptors[new_descriptor_count - 1].entry == self_mapping_pd(__vaddr))
        memset((void *)entry, 0, PAGE_SIZE);

    entry[i_pt] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PTE;
    return true;
}

static bool map_direct_large_page(uint64_t __vaddr, uint64_t __phys) noexcept {
    uint16_t i_pml4 = get_pml4_index(__vaddr);
    uint16_t i_pdpt = get_pdpt_index(__vaddr);
    uint16_t i_pd = get_pd_index(__vaddr);
    PagingDescriptorChange new_descriptors[2] = {};
    uint32_t new_descriptor_count = 0;

    uint64_t *entry = self_mapping_pml4(__vaddr);
    if (!ensure_paging_descriptor(entry, i_pml4, DEFAULT_ATTR_PML4E, new_descriptors, &new_descriptor_count)) {
        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    entry = self_mapping_pdpt(__vaddr);
    if (new_descriptor_count != 0 && new_descriptors[new_descriptor_count - 1].entry == self_mapping_pml4(__vaddr))
        memset((void *)entry, 0, PAGE_SIZE);

    if (!ensure_paging_descriptor(entry, i_pdpt, DEFAULT_ATTR_PDPTE, new_descriptors, &new_descriptor_count)) {
        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    entry = self_mapping_pd(__vaddr);
    if (new_descriptor_count != 0 && new_descriptors[new_descriptor_count - 1].entry == self_mapping_pdpt(__vaddr))
        memset((void *)entry, 0, PAGE_SIZE);

    entry[i_pd] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PDE | PAGING_DESCRIPTOR_LARGE_PAGE;
    return true;
}

static uint64_t *direct_table(uint64_t __phys) noexcept;
static bool ensure_owned_descriptor(uint64_t *__entry, uint16_t __index, uint64_t __attr, uint64_t __parent_frame,
    uint64_t __root_phys, PageTableOwner __owner, PageTableLevel __child_level,
    OwnedPagingDescriptorChange *__changes, uint32_t *__change_count) noexcept;

// Single 4 KiB mapping driven by explicit PTE attributes. Reuses the recursive
// self-mapping walk and the shared missing-level allocator. Intermediate levels
// inherit present+writable plus the user bit (when the leaf is a user mapping)
// so the leaf stays reachable; NX is encoded only on the leaf PTE. On a missing
// level allocation failure it rolls back the levels it created and returns false.
static bool map_single_page(uint64_t __vaddr, uint64_t __phys, uint64_t __attr) noexcept {
    uint16_t i_pml4 = get_pml4_index(__vaddr);
    uint16_t i_pdpt = get_pdpt_index(__vaddr);
    uint16_t i_pd = get_pd_index(__vaddr);
    uint16_t i_pt = get_pt_index(__vaddr);
    OwnedPagingDescriptorChange new_descriptors[3] = {};
    uint32_t new_descriptor_count = 0;
    const uint64_t root_phys = bigos::mm::read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK;
    const PageTableOwner owner = PageTableOwner::KernelVmem;

    uint64_t intermediate_attr = DEFAULT_ATTR_PML4E;
    if ((__attr & bigos::mm::page_attr::USER) != 0)
        intermediate_attr |= bigos::mm::page_attr::USER;

    uint64_t *entry = self_mapping_pml4(__vaddr);
    if (!ensure_owned_descriptor(entry, i_pml4, intermediate_attr, root_phys, root_phys, owner, PageTableLevel::Pdpt,
            new_descriptors, &new_descriptor_count)) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    uint64_t pdpt_phys = entry[i_pml4] & PAGING_DESCRIPTOR_ADDR_MASK;
    entry = self_mapping_pdpt(__vaddr);

    if (!ensure_owned_descriptor(entry, i_pdpt, intermediate_attr, pdpt_phys, root_phys, owner, PageTableLevel::Pd,
            new_descriptors, &new_descriptor_count)) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    uint64_t pd_phys = entry[i_pdpt] & PAGING_DESCRIPTOR_ADDR_MASK;
    entry = self_mapping_pd(__vaddr);

    if (!ensure_owned_descriptor(entry, i_pd, intermediate_attr, pd_phys, root_phys, owner, PageTableLevel::Pt,
            new_descriptors, &new_descriptor_count)) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    uint64_t pt_phys = entry[i_pd] & PAGING_DESCRIPTOR_ADDR_MASK;
    entry = self_mapping_pt(__vaddr);

    {
        bigos::irq::InterruptGuard guard;
        if (paging_present(entry[i_pt])) {
            clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
            return false;
        }
        entry[i_pt] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
        if (!increment_present_entry(pt_phys)) {
            entry[i_pt] = 0;
            clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
            return false;
        }
    }
    return true;
}

static uint64_t *direct_table(uint64_t __phys) noexcept {
    return (uint64_t *)bigos::mm::phys_to_direct(__phys & PAGING_DESCRIPTOR_ADDR_MASK);
}

static bool ensure_owned_descriptor(uint64_t *__entry, uint16_t __index, uint64_t __attr, uint64_t __parent_frame,
    uint64_t __root_phys, PageTableOwner __owner, PageTableLevel __child_level,
    OwnedPagingDescriptorChange *__changes, uint32_t *__change_count) noexcept {
    if (paging_present(__entry[__index])) {
        if ((__attr & bigos::mm::page_attr::USER) != 0 && (__entry[__index] & bigos::mm::page_attr::USER) == 0) {
            bigos::irq::InterruptGuard guard;
            __entry[__index] |= bigos::mm::page_attr::USER;
        }
        return true;
    }

    uint64_t page = (uint64_t)bigos::mm::__detail::alloc_physical_order(0, 0);
    if (page == 0)
        return false;

    uint64_t *new_table = direct_table(page);
    if (new_table == nullptr) {
        bigos::mm::__detail::free_physical_order((void *)page);
        return false;
    }
    memset(new_table, 0, PAGE_SIZE);

    if (!register_page_table(__root_phys, page, __owner, __child_level)) {
        bigos::mm::__detail::free_physical_order((void *)page);
        return false;
    }

    __changes[(*__change_count)++] = {__entry, __index, __parent_frame, page, __root_phys, __owner, __child_level};
    {
        // Ownership metadata exists before the present descriptor is published.
        bigos::irq::InterruptGuard guard;
        __entry[__index] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
        if (!increment_present_entry(__parent_frame)) {
            __entry[__index] = 0;
            unregister_page_table(page);
            bigos::mm::__detail::free_physical_order((void *)page);
            (*__change_count)--;
            return false;
        }
    }
    return true;
}

static bool ensure_root_descriptor(uint64_t *__entry, uint16_t __index, uint64_t __attr) noexcept {
    if (paging_present(__entry[__index]))
        return true;

    uint64_t page = (uint64_t)bigos::mm::__detail::alloc_physical_order(0, 0);
    if (page == 0)
        return false;

    uint64_t *new_table = direct_table(page);
    if (new_table == nullptr) {
        bigos::mm::__detail::free_physical_order((void *)page);
        return false;
    }
    memset(new_table, 0, PAGE_SIZE);

    {
        bigos::irq::InterruptGuard guard;
        __entry[__index] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
    }
    return true;
}

static uint64_t *root_pte(uint64_t __root_phys, uint64_t __vaddr) noexcept {
    uint64_t *entry = direct_table(__root_phys);
    if (entry == nullptr || !paging_present(entry[get_pml4_index(__vaddr)]))
        return nullptr;

    entry = direct_table(entry[get_pml4_index(__vaddr)]);
    if (entry == nullptr || !paging_present(entry[get_pdpt_index(__vaddr)]))
        return nullptr;

    entry = direct_table(entry[get_pdpt_index(__vaddr)]);
    if (entry == nullptr || !paging_present(entry[get_pd_index(__vaddr)]))
        return nullptr;
    if ((entry[get_pd_index(__vaddr)] & PAGING_DESCRIPTOR_LARGE_PAGE) != 0)
        return nullptr;

    entry = direct_table(entry[get_pd_index(__vaddr)]);
    if (entry == nullptr)
        return nullptr;
    return &entry[get_pt_index(__vaddr)];
}

static bool map_root_page(uint64_t __root_phys, uint64_t __vaddr, uint64_t __phys, uint64_t __attr) noexcept {
    uint16_t i_pml4 = get_pml4_index(__vaddr);
    uint16_t i_pdpt = get_pdpt_index(__vaddr);
    uint16_t i_pd = get_pd_index(__vaddr);
    uint16_t i_pt = get_pt_index(__vaddr);
    OwnedPagingDescriptorChange new_descriptors[3] = {};
    uint32_t new_descriptor_count = 0;
    const uint64_t root_phys = __root_phys & PAGING_DESCRIPTOR_ADDR_MASK;
    const PageTableOwner owner = PageTableOwner::UserAddressSpace;

    uint64_t intermediate_attr = DEFAULT_ATTR_PML4E;
    if ((__attr & bigos::mm::page_attr::USER) != 0)
        intermediate_attr |= bigos::mm::page_attr::USER;

    uint64_t *entry = direct_table(__root_phys);
    if (entry == nullptr)
        return false;
    if (!ensure_owned_descriptor(entry, i_pml4, intermediate_attr, root_phys, root_phys, owner, PageTableLevel::Pdpt,
            new_descriptors, &new_descriptor_count)) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    uint64_t pdpt_phys = entry[i_pml4] & PAGING_DESCRIPTOR_ADDR_MASK;

    entry = direct_table(entry[i_pml4]);
    if (entry == nullptr)
        return false;
    if (!ensure_owned_descriptor(entry, i_pdpt, intermediate_attr, pdpt_phys, root_phys, owner, PageTableLevel::Pd,
            new_descriptors, &new_descriptor_count)) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    uint64_t pd_phys = entry[i_pdpt] & PAGING_DESCRIPTOR_ADDR_MASK;

    entry = direct_table(entry[i_pdpt]);
    if (entry == nullptr)
        return false;
    if (!ensure_owned_descriptor(entry, i_pd, intermediate_attr, pd_phys, root_phys, owner, PageTableLevel::Pt,
            new_descriptors, &new_descriptor_count)) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }
    uint64_t pt_phys = entry[i_pd] & PAGING_DESCRIPTOR_ADDR_MASK;

    entry = direct_table(entry[i_pd]);
    if (entry == nullptr)
        return false;
    if (paging_present(entry[i_pt])) {
        clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
        return false;
    }

    {
        bigos::irq::InterruptGuard guard;
        entry[i_pt] = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
        if (!increment_present_entry(pt_phys)) {
            entry[i_pt] = 0;
            clear_owned_paging_descriptors(new_descriptors, new_descriptor_count);
            return false;
        }
    }
    return true;
}

static bool reclaim_active_empty_tables(uint64_t __root_phys, uint64_t __vaddr, PageTableOwner __owner) noexcept {
    uint64_t *pml4 = self_mapping_pml4(__vaddr);
    uint64_t *pdpt = self_mapping_pdpt(__vaddr);
    uint64_t *pd = self_mapping_pd(__vaddr);

    uint64_t &pml4e = pml4[get_pml4_index(__vaddr)];
    uint64_t &pdpte = pdpt[get_pdpt_index(__vaddr)];
    uint64_t &pde = pd[get_pd_index(__vaddr)];
    const uint64_t pdpt_phys = pml4e & PAGING_DESCRIPTOR_ADDR_MASK;
    const uint64_t pd_phys = pdpte & PAGING_DESCRIPTOR_ADDR_MASK;
    const uint64_t pt_phys = pde & PAGING_DESCRIPTOR_ADDR_MASK;

    if (page_table_owned_by(pt_phys, __root_phys, __owner, PageTableLevel::Pt) && page_table_empty(pt_phys)) {
        pde = 0;
        invalidate_active_tlb_page(__root_phys, __vaddr);
        if (!decrement_present_entry(pd_phys))
            return false;
        unregister_page_table(pt_phys);
        bigos::mm::__detail::free_physical_order((void *)pt_phys);
    } else {
        return true;
    }

    if (page_table_owned_by(pd_phys, __root_phys, __owner, PageTableLevel::Pd) && page_table_empty(pd_phys)) {
        pdpte = 0;
        invalidate_active_tlb_page(__root_phys, __vaddr);
        if (!decrement_present_entry(pdpt_phys))
            return false;
        unregister_page_table(pd_phys);
        bigos::mm::__detail::free_physical_order((void *)pd_phys);
    } else {
        return true;
    }

    if (page_table_owned_by(pdpt_phys, __root_phys, __owner, PageTableLevel::Pdpt) && page_table_empty(pdpt_phys)) {
        pml4e = 0;
        invalidate_active_tlb_page(__root_phys, __vaddr);
        unregister_page_table(pdpt_phys);
        bigos::mm::__detail::free_physical_order((void *)pdpt_phys);
    }
    return true;
}

static bool reclaim_empty_tables_in_root(
    uint64_t __root_phys, uint64_t __vaddr, PageTableOwner __owner, bool __invalidate_active) noexcept {
    const uint64_t root = __root_phys & PAGING_DESCRIPTOR_ADDR_MASK;
    uint64_t *pml4 = direct_table(root);
    if (pml4 == nullptr)
        return false;
    uint64_t &pml4e = pml4[get_pml4_index(__vaddr)];
    if (!paging_present(pml4e))
        return true;
    uint64_t *pdpt = direct_table(pml4e);
    if (pdpt == nullptr)
        return false;
    uint64_t &pdpte = pdpt[get_pdpt_index(__vaddr)];
    if (!paging_present(pdpte))
        return true;
    uint64_t *pd = direct_table(pdpte);
    if (pd == nullptr)
        return false;
    uint64_t &pde = pd[get_pd_index(__vaddr)];
    if (!paging_present(pde))
        return true;

    const uint64_t pdpt_phys = pml4e & PAGING_DESCRIPTOR_ADDR_MASK;
    const uint64_t pd_phys = pdpte & PAGING_DESCRIPTOR_ADDR_MASK;
    const uint64_t pt_phys = pde & PAGING_DESCRIPTOR_ADDR_MASK;

    if (page_table_owned_by(pt_phys, root, __owner, PageTableLevel::Pt) && page_table_empty(pt_phys)) {
        pde = 0;
        if (__invalidate_active)
            invalidate_active_tlb_page(root, __vaddr);
        if (!decrement_present_entry(pd_phys))
            return false;
        unregister_page_table(pt_phys);
        bigos::mm::__detail::free_physical_order((void *)pt_phys);
    } else {
        return true;
    }

    if (page_table_owned_by(pd_phys, root, __owner, PageTableLevel::Pd) && page_table_empty(pd_phys)) {
        pdpte = 0;
        if (__invalidate_active)
            invalidate_active_tlb_page(root, __vaddr);
        if (!decrement_present_entry(pdpt_phys))
            return false;
        unregister_page_table(pd_phys);
        bigos::mm::__detail::free_physical_order((void *)pd_phys);
    } else {
        return true;
    }

    if (page_table_owned_by(pdpt_phys, root, __owner, PageTableLevel::Pdpt) && page_table_empty(pdpt_phys)) {
        pml4e = 0;
        if (__invalidate_active)
            invalidate_active_tlb_page(root, __vaddr);
        unregister_page_table(pdpt_phys);
        bigos::mm::__detail::free_physical_order((void *)pdpt_phys);
    }
    return true;
}

static bool teardown_user_low_half(uint64_t __root_phys) noexcept {
    uint64_t *pml4 = direct_table(__root_phys);
    if (pml4 == nullptr)
        return false;

    constexpr uint32_t USER_PML4_LIMIT = 256;
    for (uint32_t pml4_i = 0; pml4_i < USER_PML4_LIMIT; pml4_i++) {
        if (!paging_present(pml4[pml4_i]))
            continue;

        const uint64_t pdpt_phys = pml4[pml4_i] & PAGING_DESCRIPTOR_ADDR_MASK;
        if (!page_table_owned_by(pdpt_phys, __root_phys, PageTableOwner::UserAddressSpace, PageTableLevel::Pdpt))
            return false;
        uint64_t *pdpt = direct_table(pdpt_phys);
        if (pdpt == nullptr)
            return false;

        for (uint32_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!paging_present(pdpt[pdpt_i]))
                continue;

            const uint64_t pd_phys = pdpt[pdpt_i] & PAGING_DESCRIPTOR_ADDR_MASK;
            if (!page_table_owned_by(pd_phys, __root_phys, PageTableOwner::UserAddressSpace, PageTableLevel::Pd))
                return false;
            uint64_t *pd = direct_table(pd_phys);
            if (pd == nullptr)
                return false;

            for (uint32_t pd_i = 0; pd_i < 512; pd_i++) {
                if (!paging_present(pd[pd_i]))
                    continue;
                if ((pd[pd_i] & PAGING_DESCRIPTOR_LARGE_PAGE) != 0)
                    return false;

                const uint64_t pt_phys = pd[pd_i] & PAGING_DESCRIPTOR_ADDR_MASK;
                if (!page_table_owned_by(pt_phys, __root_phys, PageTableOwner::UserAddressSpace, PageTableLevel::Pt))
                    return false;
                uint64_t *pt = direct_table(pt_phys);
                if (pt == nullptr)
                    return false;

                for (uint32_t pt_i = 0; pt_i < 512; pt_i++) {
                    if (!paging_present(pt[pt_i]))
                        continue;
                    const uint64_t leaf_phys = pt[pt_i] & PAGING_DESCRIPTOR_ADDR_MASK;
                    pt[pt_i] = 0;
                    if (!decrement_present_entry(pt_phys))
                        return false;
                    // Reference-count-aware release: every user leaf frame (ELF
                    // segment copies and anonymous pages alike) is allocated with
                    // an implicit owner count of one, so for a non-fork process
                    // this frees the frame exactly as before; COW-shared frames
                    // are only returned to buddy once the last owner is gone.
                    bigos::mm::frame_ref_dec_and_maybe_free(leaf_phys);
                }

                if (!page_table_empty(pt_phys))
                    return false;
                pd[pd_i] = 0;
                if (!decrement_present_entry(pd_phys))
                    return false;
                unregister_page_table(pt_phys);
                bigos::mm::__detail::free_physical_order((void *)pt_phys);
            }

            if (!page_table_empty(pd_phys))
                return false;
            pdpt[pdpt_i] = 0;
            if (!decrement_present_entry(pdpt_phys))
                return false;
            unregister_page_table(pd_phys);
            bigos::mm::__detail::free_physical_order((void *)pd_phys);
        }

        if (!page_table_empty(pdpt_phys))
            return false;
        pml4[pml4_i] = 0;
        if (!decrement_present_entry(__root_phys))
            return false;
        unregister_page_table(pdpt_phys);
        bigos::mm::__detail::free_physical_order((void *)pdpt_phys);
    }

    return page_table_empty(__root_phys);
}

static void rollback_direct_map_range(uint64_t __phys_base, uint64_t __mapped_pages) noexcept {
    uint64_t i = 0;
    while (i < __mapped_pages) {
        uint64_t vaddr = bigos::mm::KDIRECT_BASE + __phys_base + i * PAGE_SIZE;
        uint64_t *entry = self_mapping_pml4(vaddr);
        if (!paging_present(entry[get_pml4_index(vaddr)])) {
            i++;
            continue;
        }
        entry = self_mapping_pdpt(vaddr);
        if (!paging_present(entry[get_pdpt_index(vaddr)])) {
            i++;
            continue;
        }
        entry = self_mapping_pd(vaddr);
        uint64_t &pde = entry[get_pd_index(vaddr)];
        if (!paging_present(pde)) {
            i++;
            continue;
        }
        if ((pde & PAGING_DESCRIPTOR_LARGE_PAGE) != 0) {
            pde = 0;
            invalidate_active_tlb_page(bigos::mm::read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK, vaddr);
            uint64_t pages_to_next_pd = 1u << (INDEX_PD_OFFSET - INDEX_PT_OFFSET);
            i += pages_to_next_pd - (i % pages_to_next_pd);
            continue;
        }

        uint64_t *pte = &self_mapping_pt(vaddr)[get_pt_index(vaddr)];
        if (paging_present(*pte)) {
            *pte = 0;
            invalidate_active_tlb_page(bigos::mm::read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK, vaddr);
        }
        i++;
    }
}

static void map_direct_range(uint64_t __phys_base, uint64_t __len) noexcept {
    uint64_t mapped_pages = 0;
    uint64_t phys = __phys_base;
    uint64_t remaining = __len;
    constexpr uint64_t LARGE_PAGE_SIZE = 1ul << INDEX_PD_OFFSET;

    while (remaining != 0) {
        bool use_large_page = (phys % LARGE_PAGE_SIZE) == 0 && remaining >= LARGE_PAGE_SIZE;
        uint64_t step = use_large_page ? LARGE_PAGE_SIZE : PAGE_SIZE;
        bool mapped = use_large_page ? map_direct_large_page(bigos::mm::KDIRECT_BASE + phys, phys)
                                     : map_direct_page(bigos::mm::KDIRECT_BASE + phys, phys);
        if (!mapped) {
            rollback_direct_map_range(__phys_base, mapped_pages);
            direct_map_panic("BIGOS_DIRECT_MAP_INIT_FAILED page-table allocation");
        }
        mapped_pages += step / PAGE_SIZE;
        phys += step;
        remaining -= step;
    }

    if (!direct_map_record_range(__phys_base, __len))
        direct_map_panic("BIGOS_DIRECT_MAP_INIT_FAILED range table exhausted");
}

static bool direct_map_memory_type_is_ram(uint32_t __type) noexcept {
    return __type == BIGOS_BOOT_MEMORY_TYPE_USABLE;
}

static void init_direct_map_from_region(uint64_t __base, uint64_t __len, uint32_t __type) noexcept {
    if (!direct_map_memory_type_is_ram(__type))
        return;

    uint64_t aligned_base = 0;
    uint64_t aligned_len = 0;
    if (!direct_map_select_range(__base, __len, &aligned_base, &aligned_len))
        return;

    map_direct_range(aligned_base, aligned_len);
}

static void init_direct_map_v2(const BootInfoHeader *__header) noexcept {
    const BootInfoSection *section = bigos_boot_info_v2_find_section(__header, BIGOS_BOOT_SECTION_TYPE_MEMORY_MAP);
    if (section == nullptr || section->size % sizeof(BootMemoryRegion) != 0)
        direct_map_panic("BIGOS_DIRECT_MAP_INIT_FAILED invalid boot memory map");

    const BootMemoryRegion *regions = (const BootMemoryRegion *)((const uint8_t *)__header + section->offset);
    uint32_t nr_regions = section->size / sizeof(BootMemoryRegion);
    for (uint32_t i = 0; i < nr_regions; i++)
        init_direct_map_from_region(regions[i].physical_base, regions[i].length, regions[i].normalized_type);
}

static uint32_t normalize_legacy_ards_type(uint32_t __type) noexcept {
    switch (__type) {
        case 1:
            return BIGOS_BOOT_MEMORY_TYPE_USABLE;
        case 3:
            return BIGOS_BOOT_MEMORY_TYPE_ACPI_RECLAIM;
        case 4:
            return BIGOS_BOOT_MEMORY_TYPE_ACPI_NVS;
        case 5:
            return BIGOS_BOOT_MEMORY_TYPE_BAD_MEMORY;
        default:
            return BIGOS_BOOT_MEMORY_TYPE_RESERVED;
    }
}

static void init_direct_map_v1(const BootInfo *__info) noexcept {
    struct LegacyARDS {
        uint64_t base;
        uint64_t len;
        uint32_t type;
        uint32_t attributes;
    };

    const LegacyARDS *ards_arr = (const LegacyARDS *)__info->e820_entry_address;
    for (uint32_t i = 0; i < __info->e820_entry_count; i++)
        init_direct_map_from_region(ards_arr[i].base, ards_arr[i].len, normalize_legacy_ards_type(ards_arr[i].type));
}

NAMESPACE_BIGOS_BEG
namespace mm {
    void invalidate_tlb(const TlbInvalidationRequest &__request) noexcept {
        // Order page-table writes before the local invalidation completion point.
        asm volatile("" ::: "memory");

        const uint64_t current_bit = 1ull << bigos::cpu::current_cpu_id();
        uint64_t target_mask = __request.target_cpu_mask;
        if (__request.mm_context != nullptr)
            target_mask |= mm_context_target_mask(__request.mm_context);
        if (target_mask == 0)
            target_mask = current_bit;

        if ((target_mask & current_bit) != 0)
            perform_local_tlb_invalidation(__request.scope, __request.start_vaddr, __request.length);

        const uint64_t remote_mask = target_mask & ~current_bit;
        if (remote_mask != 0) {
            shootdown_lock(&g_tlb_shootdown_slot.lock);
            g_tlb_shootdown_slot.scope = __request.scope;
            g_tlb_shootdown_slot.address_space_root = __request.address_space_root & PAGING_DESCRIPTOR_ADDR_MASK;
            g_tlb_shootdown_slot.start_vaddr = __request.start_vaddr;
            g_tlb_shootdown_slot.length = __request.length;
            g_tlb_shootdown_slot.target_cpu_mask = remote_mask;
            g_tlb_shootdown_slot.ack_cpu_mask = 0;
            g_tlb_shootdown_slot.generation++;
            __atomic_store_n(&g_tlb_shootdown_slot.active, true, __ATOMIC_RELEASE);

            bigos::smp::IpiDeliveryResult failure = {};
            const uint64_t delivered =
                bigos::smp::send_ipi_mask(remote_mask, bigos::smp::IpiType::TlbShootdown, &failure);
            if (delivered != remote_mask) {
                __atomic_store_n(&g_tlb_shootdown_slot.active, false, __ATOMIC_RELEASE);
                shootdown_unlock(&g_tlb_shootdown_slot.lock);
                bigos::kpanic(bigos::PanicCode::Generic, "mm-tlb", "TLB shootdown IPI delivery failed\n");
            }
            if (__request.require_completion && !wait_for_tlb_shootdown_ack(remote_mask)) {
                __atomic_store_n(&g_tlb_shootdown_slot.active, false, __ATOMIC_RELEASE);
                shootdown_unlock(&g_tlb_shootdown_slot.lock);
                bigos::kpanic(bigos::PanicCode::Generic, "mm-tlb", "TLB shootdown ack timeout\n");
            }
            __atomic_store_n(&g_tlb_shootdown_slot.active, false, __ATOMIC_RELEASE);
            shootdown_unlock(&g_tlb_shootdown_slot.lock);
#ifdef BIGOS_TLB_SHOOTDOWN_SMOKE
            bigos::serial_puts("BIGOS_TLB_SHOOTDOWN_COMPLETE\n");
#endif
        }

        if (__request.require_completion)
            asm volatile("" ::: "memory");
    }

    void handle_tlb_shootdown_ipi() noexcept {
        if (!__atomic_load_n(&g_tlb_shootdown_slot.active, __ATOMIC_ACQUIRE))
            return;
        const uint64_t bit = 1ull << bigos::cpu::current_cpu_id();
        const uint64_t target = __atomic_load_n(&g_tlb_shootdown_slot.target_cpu_mask, __ATOMIC_ACQUIRE);
        if ((target & bit) == 0)
            return;

        perform_local_tlb_invalidation(
            g_tlb_shootdown_slot.scope, g_tlb_shootdown_slot.start_vaddr, g_tlb_shootdown_slot.length);
        __atomic_fetch_or(&g_tlb_shootdown_slot.ack_cpu_mask, bit, __ATOMIC_ACQ_REL);
    }

    namespace __detail {
        void init_direct_map(const BootInfoHeader *__boot_info) {
            gDirectMapRangeCount = 0;

            BootHandoff handoff = bigos_boot_resolve_handoff(__boot_info);
            if (handoff.v2 != nullptr) {
                init_direct_map_v2(handoff.v2);
            } else if (handoff.v1 != nullptr) {
                init_direct_map_v1(handoff.v1);
            } else {
                direct_map_panic("BIGOS_DIRECT_MAP_INIT_FAILED invalid boot handoff");
            }
        }

        void init_vmem() {
            MemoryBlock *mblk = new MemoryBlock();
            // mblk->vmem = &kvmem;
            // KVMEM is a kernel heap/vmalloc-style virtual allocation area,
            // not a direct map and not a virt = phys + offset region.
            mblk->base = KVMEM_BASE;
            // mblk->len = KVMEM_LEN;
            mblk->nr_pages = KVMEM_LEN / PAGE_SIZE;
            mblk->flags = 0;

            auto node = new ktl::intrusive_list_node<MemoryBlock *>(mblk);

            kvmem.pml4_ = (pml4_t)KERNEL_PML4_ADDR;
            kvmem.free_area_.insert(node);
            kvmem.nr_pages_ = mblk->nr_pages;
            kvmem.nr_free_pages_ = mblk->nr_pages;
        }

        uint32_t kernel_vmem_free_pages() noexcept {
            bigos::irq::InterruptGuard guard;
            return kvmem.nr_free_pages();
        }

        bool clone_kernel_page_mapping_in_root(uint64_t __root_phys, uint64_t __vaddr) noexcept {
            if (__root_phys == INVALID_PHYS_ADDR || (__vaddr & (PAGE_SIZE - 1)) != 0)
                return false;

            uint64_t *dst_pml4 = direct_table(__root_phys);
            uint64_t *kernel_pml4 = self_mapping_pml4(0);
            const uint16_t pml4_index = get_pml4_index(__vaddr);
            if (dst_pml4 == nullptr || kernel_pml4 == nullptr || !paging_present(kernel_pml4[pml4_index]))
                return false;

            {
                bigos::irq::InterruptGuard guard;
                dst_pml4[pml4_index] = kernel_pml4[pml4_index];
            }

            uint64_t *source_pte = mapped_pte(__vaddr);
            if (source_pte == nullptr || !paging_present(*source_pte))
                return false;

            const uint64_t phys = *source_pte & PAGING_DESCRIPTOR_ADDR_MASK;
            const PageAttr attr = *source_pte & ~PAGING_DESCRIPTOR_ADDR_MASK;
            uint64_t *dest_pte = root_pte(__root_phys, __vaddr);
            if (dest_pte != nullptr) {
                if (paging_present(*dest_pte))
                    return (*dest_pte & PAGING_DESCRIPTOR_ADDR_MASK) == phys;
                bigos::irq::InterruptGuard guard;
                *dest_pte = phys | attr;
                return true;
            }
            return map_page_in_root(__root_phys, __vaddr, phys, attr);
        }

        bool user_range_writable(uint64_t __root_phys, uint64_t __vaddr, uint64_t __len) noexcept {
            constexpr uint64_t USER_CANONICAL_LIMIT = 0x0000800000000000ull;
            if (__len == 0 || __root_phys == INVALID_PHYS_ADDR || __vaddr >= USER_CANONICAL_LIMIT)
                return false;
            const uint64_t end = __vaddr + __len;
            if (end <= __vaddr || end > USER_CANONICAL_LIMIT)
                return false;

            uint64_t page = align_down_page(__vaddr);
            const uint64_t last = align_down_page(end - 1);
            while (page <= last) {
                uint64_t *pte = root_pte(__root_phys, page);
                if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0 ||
                    (*pte & page_attr::WRITABLE) == 0)
                    return false;
                if (page == last)
                    break;
                page += PAGE_SIZE;
            }
            return true;
        }

        bool copy_from_user_root(uint64_t __root_phys, uint64_t __addr, void *__dst, uint64_t __len) noexcept {
            if (__dst == nullptr || __len == 0)
                return false;

            auto *dst = (uint8_t *)__dst;
            for (uint64_t copied = 0; copied < __len;) {
                const uint64_t vaddr = __addr + copied;
                uint64_t *pte = root_pte(__root_phys, vaddr);
                if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0)
                    return false;

                const uint64_t page_offset = vaddr & (PAGE_SIZE - 1);
                uint64_t chunk = PAGE_SIZE - page_offset;
                if (chunk > __len - copied)
                    chunk = __len - copied;

                const uint64_t phys = (*pte & PAGING_DESCRIPTOR_ADDR_MASK) + page_offset;
                void *src = phys_to_direct(phys);
                if (src == nullptr)
                    return false;
                memcpy(dst + copied, src, chunk);
                copied += chunk;
            }
            return true;
        }

        bool copy_to_user_root(uint64_t __root_phys, uint64_t __addr, const void *__src, uint64_t __len) noexcept {
            if (__src == nullptr || __len == 0)
                return false;

            const auto *src = (const uint8_t *)__src;
            for (uint64_t copied = 0; copied < __len;) {
                const uint64_t vaddr = __addr + copied;
                uint64_t *pte = root_pte(__root_phys, vaddr);
                if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0 ||
                    (*pte & page_attr::WRITABLE) == 0)
                    return false;

                const uint64_t page_offset = vaddr & (PAGE_SIZE - 1);
                uint64_t chunk = PAGE_SIZE - page_offset;
                if (chunk > __len - copied)
                    chunk = __len - copied;

                const uint64_t phys = (*pte & PAGING_DESCRIPTOR_ADDR_MASK) + page_offset;
                void *dst = phys_to_direct(phys);
                if (dst == nullptr)
                    return false;
                memcpy(dst, src + copied, chunk);
                copied += chunk;
            }
            return true;
        }

        bool unmap_user_page_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t *__phys) noexcept {
            if (__phys != nullptr)
                *__phys = INVALID_PHYS_ADDR;
            if (__root_phys == INVALID_PHYS_ADDR || (__vaddr & (PAGE_SIZE - 1)) != 0)
                return false;

            uint64_t *pte = root_pte(__root_phys, __vaddr);
            if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0)
                return false;

            const uint64_t phys = *pte & PAGING_DESCRIPTOR_ADDR_MASK;
            const uint64_t root = __root_phys & PAGING_DESCRIPTOR_ADDR_MASK;
            uint64_t *pml4 = direct_table(root);
            if (pml4 == nullptr)
                return false;
            uint64_t *pdpt = direct_table(pml4[get_pml4_index(__vaddr)]);
            if (pdpt == nullptr)
                return false;
            uint64_t *pd = direct_table(pdpt[get_pdpt_index(__vaddr)]);
            if (pd == nullptr)
                return false;
            const uint64_t pt_phys = pd[get_pd_index(__vaddr)] & PAGING_DESCRIPTOR_ADDR_MASK;

            {
                bigos::irq::InterruptGuard guard;
                *pte = 0;
                if ((__root_phys & PAGING_DESCRIPTOR_ADDR_MASK) == (read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK))
                    invalidate_active_tlb_page(__root_phys, __vaddr);
            }
            if (!decrement_present_entry(pt_phys))
                return false;
            if (__phys != nullptr)
                *__phys = phys;
            return true;
        }
    }   // namespace __detail

    bool unmap_user_page_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t *__phys) noexcept {
        if (__phys != nullptr)
            *__phys = INVALID_PHYS_ADDR;
        if (__root_phys == INVALID_PHYS_ADDR || (__vaddr & (PAGE_SIZE - 1)) != 0)
            return false;

        uint64_t *pte = root_pte(__root_phys, __vaddr);
        if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0)
            return false;

        const uint64_t root = __root_phys & PAGING_DESCRIPTOR_ADDR_MASK;
        const bool active = root == (read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK);
        const uint64_t phys = *pte & PAGING_DESCRIPTOR_ADDR_MASK;
        uint64_t *pml4 = direct_table(root);
        if (pml4 == nullptr)
            return false;
        uint64_t *pdpt = direct_table(pml4[get_pml4_index(__vaddr)]);
        if (pdpt == nullptr)
            return false;
        uint64_t *pd = direct_table(pdpt[get_pdpt_index(__vaddr)]);
        if (pd == nullptr)
            return false;
        const uint64_t pt_phys = pd[get_pd_index(__vaddr)] & PAGING_DESCRIPTOR_ADDR_MASK;

        {
            bigos::irq::InterruptGuard guard;
            *pte = 0;
            if (active)
                invalidate_active_tlb_page(root, __vaddr);
        }
        if (!decrement_present_entry(pt_phys))
            return false;
        if (!reclaim_empty_tables_in_root(root, __vaddr, PageTableOwner::UserAddressSpace, active))
            return false;
        if (__phys != nullptr)
            *__phys = phys;
        return true;
    }

    bool is_direct_mapped_phys(uint64_t __phys, uint64_t __len) noexcept {
        return direct_map_range_covered(__phys, __len);
    }

    void *phys_to_direct(uint64_t __phys) noexcept {
        if (!is_direct_mapped_phys(__phys, 1))
            return nullptr;
        return (void *)(KDIRECT_BASE + __phys);
    }

    uint64_t direct_to_phys(const void *__addr) noexcept {
        uint64_t addr = (uint64_t)__addr;
        if (addr < KDIRECT_BASE || addr >= KDIRECT_BASE + KDIRECT_LEN)
            return INVALID_PHYS_ADDR;

        uint64_t phys = addr - KDIRECT_BASE;
        if (!is_direct_mapped_phys(phys, 1))
            return INVALID_PHYS_ADDR;
        return phys;
    }

    bool map_page(uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept {
        return map_single_page(__vaddr, __phys, __attr);
    }

    void unmap_page(uint64_t __vaddr) noexcept {
        uint64_t *pml4 = self_mapping_pml4(__vaddr);
        if (!paging_present(pml4[get_pml4_index(__vaddr)]))
            return;
        uint64_t *pdpt = self_mapping_pdpt(__vaddr);
        if (!paging_present(pdpt[get_pdpt_index(__vaddr)]))
            return;
        uint64_t *pd = self_mapping_pd(__vaddr);
        if (!paging_present(pd[get_pd_index(__vaddr)]) || (pd[get_pd_index(__vaddr)] & PAGING_DESCRIPTOR_LARGE_PAGE) != 0)
            return;

        uint64_t *pte = &self_mapping_pt(__vaddr)[get_pt_index(__vaddr)];
        if (paging_present(*pte)) {
            const uint64_t root_phys = read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK;
            const uint64_t pt_phys = pd[get_pd_index(__vaddr)] & PAGING_DESCRIPTOR_ADDR_MASK;
            {
                bigos::irq::InterruptGuard guard;
                *pte = 0;
                invalidate_active_tlb_page(root_phys, __vaddr);
            }
            if (!decrement_present_entry(pt_phys) ||
                !reclaim_active_empty_tables(root_phys, __vaddr, PageTableOwner::KernelVmem))
                direct_map_panic("BIGOS_PAGE_TABLE_RECLAIM_FAILED active-unmap");
        }
    }

    uint64_t derive_user_address_space_root() noexcept {
        uint64_t new_root = (uint64_t)__detail::alloc_physical_order(0, 0);
        if (new_root == 0)
            return INVALID_PHYS_ADDR;

        // The newly allocated frame comes from usable RAM, which the direct map
        // covers; access it through the direct mapping to publish entries.
        uint64_t *dst = (uint64_t *)phys_to_direct(new_root);
        if (dst == nullptr) {
            __detail::free_physical_order((void *)new_root);
            return INVALID_PHYS_ADDR;
        }

        // The recursive self-mapping resolves the active (kernel) PML4.
        const uint64_t *kernel_pml4 = self_mapping_pml4(0);
        constexpr uint32_t HALF = 256;   // index 0..255 = lower half, 256..511 = higher half
        for (uint32_t i = 0; i < HALF; i++)
            dst[i] = 0;   // user lower half: independent, cleared
        for (uint32_t i = HALF; i < 512; i++)
            dst[i] = kernel_pml4[i];   // share kernel higher half / self-mapping / direct map

        if (!register_page_table(new_root, new_root, PageTableOwner::UserAddressSpace, PageTableLevel::Pml4)) {
            __detail::free_physical_order((void *)new_root);
            return INVALID_PHYS_ADDR;
        }

        return new_root;
    }

    bool map_page_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept {
        if (__root_phys == INVALID_PHYS_ADDR || (__vaddr & (PAGE_SIZE - 1)) != 0 || (__phys & (PAGE_SIZE - 1)) != 0)
            return false;
        return map_root_page(__root_phys, __vaddr, __phys, __attr);
    }

    bool user_range_mapped(uint64_t __root_phys, uint64_t __vaddr, uint64_t __len) noexcept {
        constexpr uint64_t USER_CANONICAL_LIMIT = 0x0000800000000000ull;
        if (__len == 0 || __root_phys == INVALID_PHYS_ADDR || __vaddr >= USER_CANONICAL_LIMIT)
            return false;
        const uint64_t end = __vaddr + __len;
        if (end <= __vaddr || end > USER_CANONICAL_LIMIT)
            return false;

        uint64_t page = align_down_page(__vaddr);
        const uint64_t last = align_down_page(end - 1);
        while (page <= last) {
            uint64_t *pte = root_pte(__root_phys, page);
            if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0)
                return false;
            if (page == last)
                break;
            page += PAGE_SIZE;
        }
        return true;
    }

    bool teardown_user_address_space(uint64_t __root_phys) noexcept {
        if (__root_phys == INVALID_PHYS_ADDR)
            return false;
        const uint64_t root = __root_phys & PAGING_DESCRIPTOR_ADDR_MASK;
        if (root == (read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK))
            return false;
        if (!page_table_owned_by(root, root, PageTableOwner::UserAddressSpace, PageTableLevel::Pml4))
            return false;

        // The root is inactive here, so no immediate invlpg is required. The
        // single-core runtime must not reactivate it after this teardown starts.
        if (!teardown_user_low_half(root))
            return false;

        unregister_page_table(root);
        __detail::free_physical_order((void *)root);
        return true;
    }

    void init_frame_refcount() noexcept {
        constexpr uint32_t PAGE_SHIFT = 12;
        gFrameRefcount = nullptr;
        gFrameRefcountMaxFrame = 0;

        // Highest physical address covered by the direct map bounds the set of
        // allocatable user frames; the buddy allocator only hands back RAM that
        // the direct map covers, so this is a safe upper bound for the table.
        uint64_t max_phys_end = 0;
        for (uint32_t i = 0; i < gDirectMapRangeCount; i++) {
            const uint64_t end = gDirectMapRanges[i].base + gDirectMapRanges[i].len;
            if (end > max_phys_end)
                max_phys_end = end;
        }
        if (max_phys_end == 0)
            return;

        uint64_t max_frame = (max_phys_end - 1) >> PAGE_SHIFT;
        uint64_t entries = max_frame + 1;
        uint64_t bytes = entries * sizeof(uint16_t);
        uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

        uint32_t order = 0;
        while ((1ull << order) < pages && order < BUDDY_MAX_ORDER)
            order++;
        // Clamp the covered range to one contiguous buddy block. Frames beyond
        // the table are simply untracked: frame_ref_inc() fails deterministically
        // on them (forcing a fork rollback) and frame_ref_dec_and_maybe_free()
        // frees them directly, so safety is preserved.
        if ((1ull << order) < pages) {
            pages = 1ull << BUDDY_MAX_ORDER;
            entries = pages * PAGE_SIZE / sizeof(uint16_t);
            max_frame = entries - 1;
        }

        uint64_t phys = (uint64_t)__detail::alloc_physical_order(order, 0);
        if (phys == 0)
            return;
        void *table = phys_to_direct(phys);
        if (table == nullptr) {
            __detail::free_physical_order((void *)phys);
            return;
        }
        memset(table, 0, (size_t)(pages * PAGE_SIZE));
        gFrameRefcount = (uint16_t *)table;
        gFrameRefcountMaxFrame = max_frame;
    }

    static uint16_t *frame_refcount_slot(uint64_t __phys) noexcept {
        if (gFrameRefcount == nullptr)
            return nullptr;
        const uint64_t frame = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) >> 12;
        if (frame > gFrameRefcountMaxFrame)
            return nullptr;
        return &gFrameRefcount[frame];
    }

    bool frame_ref_inc(uint64_t __phys) noexcept {
        uint16_t *slot = frame_refcount_slot(__phys);
        if (slot == nullptr)
            return false;
        // 0 encodes the lone implicit owner (count == 1); the first share moves
        // the true count to 2. Saturation is a deterministic failure, never a
        // wrap-around, so the caller can roll back the fork.
        if (*slot == 0) {
            *slot = 2;
            return true;
        }
        if (*slot == FRAME_REFCOUNT_MAX)
            return false;
        (*slot)++;
        return true;
    }

    void frame_ref_dec_and_maybe_free(uint64_t __phys) noexcept {
        const uint64_t frame_phys = __phys & PAGING_DESCRIPTOR_ADDR_MASK;
        uint16_t *slot = frame_refcount_slot(frame_phys);
        if (slot == nullptr || *slot == 0) {
            // Untracked or lone owner (count == 1): this decrement releases it.
            __detail::free_physical_order((void *)frame_phys);
            return;
        }
        (*slot)--;
        // Collapse a remaining count of one back to the lone-owner encoding so a
        // later teardown of that last owner frees the frame exactly once.
        if (*slot == 1)
            *slot = 0;
    }

    bool frame_ref_is_shared(uint64_t __phys) noexcept {
        uint16_t *slot = frame_refcount_slot(__phys);
        // 0 encodes the lone implicit owner; any value >= 2 means shared.
        return slot != nullptr && *slot >= 2;
    }

    bool read_user_leaf_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t *__phys, PageAttr *__attr) noexcept {
        if (__phys != nullptr)
            *__phys = INVALID_PHYS_ADDR;
        if (__attr != nullptr)
            *__attr = 0;
        if (__root_phys == INVALID_PHYS_ADDR || (__vaddr & (PAGE_SIZE - 1)) != 0)
            return false;
        uint64_t *pte = root_pte(__root_phys, __vaddr);
        if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0)
            return false;
        if (__phys != nullptr)
            *__phys = *pte & PAGING_DESCRIPTOR_ADDR_MASK;
        if (__attr != nullptr)
            *__attr = *pte & ~PAGING_DESCRIPTOR_ADDR_MASK;
        return true;
    }

    bool remap_user_page_in_root(uint64_t __root_phys, uint64_t __vaddr, uint64_t __phys, PageAttr __attr) noexcept {
        if (__root_phys == INVALID_PHYS_ADDR || (__vaddr & (PAGE_SIZE - 1)) != 0 || (__phys & (PAGE_SIZE - 1)) != 0)
            return false;
        uint64_t *pte = root_pte(__root_phys, __vaddr);
        if (pte == nullptr || !paging_present(*pte) || (*pte & page_attr::USER) == 0)
            return false;
        {
            bigos::irq::InterruptGuard guard;
            *pte = (__phys & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
            if ((__root_phys & PAGING_DESCRIPTOR_ADDR_MASK) == (read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK))
                invalidate_active_tlb_page(__root_phys, __vaddr);
        }
        return true;
    }

    uint64_t read_cr3() noexcept {
        uint64_t cr3;
        asm volatile("movq %%cr3, %0" : "=r"(cr3));
        return cr3;
    }

    void activate_address_space_root(uint64_t __root_phys) noexcept {
        asm volatile("movq %0, %%cr3" ::"r"(__root_phys) : "memory");
        bigos::cpu::set_current_address_space_root(__root_phys & PAGING_DESCRIPTOR_ADDR_MASK);
    }

#ifdef BIGOS_USER_VMEM_SMOKE
    bool user_vmem_smoke() noexcept {
        // Lower-half (user region) test address, page-aligned and unused by the
        // kernel. We only build/read the entry; we never switch CR3 or enter ring3.
        constexpr uint64_t TEST_VADDR = 0x0000000001000000ul;

        uint64_t phys = (uint64_t)__detail::alloc_physical_order(0, 0);
        if (phys == 0) {
            bigos::serial_puts("BIGOS_USER_VMEM_SMOKE_FAILED alloc\n");
            return false;
        }

        bool ok = map_page(TEST_VADDR, phys, page_attr::USER_DATA);
        if (!ok) {
            __detail::free_physical_order((void *)phys);
            bigos::serial_puts("BIGOS_USER_VMEM_SMOKE_FAILED map\n");
            return false;
        }

        uint64_t *pte = mapped_pte(TEST_VADDR);
        bool pte_ok = pte != nullptr && paging_present(*pte) && (*pte & page_attr::USER) != 0 &&   // user bit set
                      (*pte & page_attr::WRITABLE) != 0 &&                                         // writable
                      (*pte & page_attr::NO_EXECUTE) != 0;   // user data page is NX-encoded
        uint64_t pte_value = pte != nullptr ? *pte : 0;

        unmap_page(TEST_VADDR);
        __detail::free_physical_order((void *)phys);

        // Encoding check for a user code page: same attribute set must clear NX.
        bool code_attr_ok =
            (page_attr::USER_CODE & page_attr::NO_EXECUTE) == 0 && (page_attr::USER_CODE & page_attr::USER) != 0;

        // Derive a user root and confirm the higher/lower half layout.
        uint64_t root = derive_user_address_space_root();
        bool root_ok = false;
        if (root != INVALID_PHYS_ADDR) {
            const uint64_t *root_entries = (const uint64_t *)phys_to_direct(root);
            const uint64_t *kernel_pml4 = self_mapping_pml4(0);
            if (root_entries != nullptr) {
                root_ok = true;
                for (uint32_t i = 0; i < 256; i++) {
                    if (root_entries[i] != 0) {
                        root_ok = false;
                        break;
                    }
                }
                for (uint32_t i = 256; root_ok && i < 512; i++) {
                    if (root_entries[i] != kernel_pml4[i]) {
                        root_ok = false;
                        break;
                    }
                }
            }
            (void)teardown_user_address_space(root);
        }

        if (pte_ok && code_attr_ok && root_ok) {
            bigos::serial_puts("BIGOS_USER_VMEM_SMOKE_PASSED\n");
            return true;
        }

        (void)pte_value;
        bigos::serial_puts("BIGOS_USER_VMEM_SMOKE_FAILED verify\n");
        return false;
    }
#endif

    void VMem::rollback_kernel_range(uint64_t __base, uint32_t __nr_pages) noexcept {
        for (uint32_t i = 0; i < __nr_pages; i++) {
            uint64_t vaddr = __base + i * PAGE_SIZE;
            uint64_t *pte = mapped_pte(vaddr);
            if (pte != nullptr && paging_present(*pte)) {
                bigos::irq::InterruptGuard guard;
                *pte = 0;
                invalidate_active_tlb_page(read_cr3() & PAGING_DESCRIPTOR_ADDR_MASK, vaddr);
            }
        }
    }

    bool VMem::map_kernel_range(MemoryBlock *__mblk) noexcept {
        if (__mblk == nullptr)
            return false;

        uint64_t base = __mblk->base;
        uint32_t mapped_pages = 0;

        for (auto physical_m : __mblk->physical_area) {
            uint32_t nr_physical_pages = physical_m.second;
            uint64_t physical_base = (uint64_t)physical_m.first;

            while (nr_physical_pages) {
                // Kernel ranges use the supervisor default attribute, which is
                // bit-for-bit equivalent to the legacy DEFAULT_ATTR_PTE = 0x3
                // (present + writable, user=0, NX=0).
                if (!map_single_page(base, physical_base, page_attr::KERNEL_DEFAULT)) {
                    rollback_kernel_range(__mblk->base, mapped_pages);
                    return false;
                }

                mapped_pages++;
                nr_physical_pages--;

                base += PAGE_SIZE;
                physical_base += PAGE_SIZE;
            }
        }

        return true;
    }

    void VMem::unmap_kernel_range(MemoryBlock *__mblk) noexcept {
        if (__mblk == nullptr || __mblk->physical_area.empty())
            return;

        uint32_t mapped_pages = 0;
        for (auto physical_m : __mblk->physical_area)
            mapped_pages += physical_m.second;

        if (mapped_pages > __mblk->nr_pages)
            mapped_pages = __mblk->nr_pages;

        rollback_kernel_range(__mblk->base, mapped_pages);
    }

    void VMem::release_physical_area(MemoryBlock *__mblk) noexcept {
        if (__mblk == nullptr)
            return;

        for (auto physical_pair : __mblk->physical_area) {
            __detail::free_physical_order(physical_pair.first);
        }

        while (!__mblk->physical_area.empty()) {
            bigos::irq::InterruptGuard guard;
            auto temp = __mblk->physical_area.begin();
            auto temp_node = temp._node;
            __mblk->physical_area.erase(temp);
            delete temp_node;
        }
    }

    void VMem::merge(ktl::intrusive_list_node<MemoryBlock *> *__mblk_node) noexcept {
        MemoryBlock *mblk = **__mblk_node;

        uint64_t end_addr;
        MemoryBlock *adjacent_mblk = nullptr;
        ktl::intrusive_list_node<MemoryBlock *> *adjacent_mblk_node = nullptr;

        auto iter = ktl::intrusive_list<MemoryBlock *>::iterator(__mblk_node);
        auto iter_next = iter;
        ++iter_next;

        // try to merge with next one
        if (iter_next != free_area_.end()) {
            adjacent_mblk_node = (ktl::intrusive_list_node<MemoryBlock *> *)iter_next._node;
            adjacent_mblk = *iter_next;
            end_addr = mblk->base + mblk->nr_pages * PAGE_SIZE;
            if (end_addr == adjacent_mblk->base) {
                free_area_.erase(iter);
                free_area_.erase(iter_next);

                mblk->nr_pages += adjacent_mblk->nr_pages;

                delete adjacent_mblk;
                delete adjacent_mblk_node;

                iter = free_area_.begin();
                while (iter != free_area_.end()) {
                    if ((*iter)->base > mblk->base)
                        break;
                    iter++;
                }

                free_area_.insert(iter, __mblk_node);
                merge(__mblk_node);
                return;
            }
        }

        // try to merge with previous one
        iter = ktl::intrusive_list<MemoryBlock *>::iterator(__mblk_node);
        if (iter != free_area_.begin()) {
            auto iter_prev = iter;
            --iter_prev;
            adjacent_mblk_node = (ktl::intrusive_list_node<MemoryBlock *> *)iter_prev._node;
            adjacent_mblk = *iter_prev;

            end_addr = adjacent_mblk->base + adjacent_mblk->nr_pages * PAGE_SIZE;
            if (end_addr == mblk->base) {
                free_area_.erase(iter);
                free_area_.erase(iter_prev);

                adjacent_mblk->nr_pages += mblk->nr_pages;

                delete mblk;
                delete __mblk_node;

                iter = free_area_.begin();
                while (iter != free_area_.end()) {
                    if ((*iter)->base > adjacent_mblk->base)
                        break;
                    iter++;
                }

                free_area_.insert(iter, adjacent_mblk_node);
                merge(adjacent_mblk_node);
                return;
            }
        }
    }

    void VMem::__free(const void *__p) noexcept {
        if (__p == nullptr)
            return;

        bigos::irq::InterruptGuard guard;
        uint64_t addr = (uint64_t)__p;

        auto iter = used_area_.begin();
        while (iter != used_area_.end()) {
            if ((*iter)->base == addr)
                break;
            iter++;
        }

        if (iter == used_area_.end())
            return;

        auto mblk = *iter;
        auto mblk_node = iter._node;
        used_area_.erase(iter);

        unmap_kernel_range(mblk);
        release_physical_area(mblk);

        nr_free_pages_ += mblk->nr_pages;

        auto insert_position = free_area_.begin();
        while (insert_position != free_area_.end()) {
            if ((*insert_position)->base > mblk->base)
                break;
            insert_position++;
        }

        free_area_.insert(insert_position, mblk_node);
        merge((ktl::intrusive_list_node<MemoryBlock *> *)mblk_node);
    }

    MemoryBlock *VMem::__alloc_pages(uint32_t __pages, gfm_t __gfm) noexcept {
        if (__pages == 0 || nr_free_pages_ < __pages)
            return nullptr;

        bigos::irq::InterruptGuard guard;
        auto iter = free_area_.begin();
        while (iter != free_area_.end()) {
            if ((*iter)->nr_pages >= __pages)
                break;
            iter++;
        }

        if (iter == free_area_.end())
            return nullptr;

        auto mblk = *iter;
        auto mblk_node = iter._node;
        free_area_.erase(iter);

        // divide
        if (mblk->nr_pages > __pages) {
            auto new_mblk = new MemoryBlock();
            if (new_mblk == nullptr) {
                free_area_.insert(mblk_node);
                return nullptr;
            }

            new_mblk->base = mblk->base + PAGE_SIZE * __pages;
            new_mblk->flags = mblk->flags;
            new_mblk->nr_pages = mblk->nr_pages - __pages;

            mblk->nr_pages = __pages;

            auto node = new ktl::intrusive_list_node<MemoryBlock *>(new_mblk);
            if (node == nullptr) {
                mblk->nr_pages += new_mblk->nr_pages;
                delete new_mblk;
                free_area_.insert(mblk_node);
                return nullptr;
            }

            auto insert_position = free_area_.begin();
            while (insert_position != free_area_.end()) {
                if ((*insert_position)->base > new_mblk->base)
                    break;
                insert_position++;
            }
            free_area_.insert(insert_position, node);
        }

        used_area_.insert(mblk_node);
        nr_free_pages_ -= __pages;
        return mblk;
    }
}   // namespace mm

// defined in memory.h
void *alloc_kernel_pages(uint32_t __pages, gfm_t __gfm) noexcept {
    mm::MemoryBlock *mblk = kvmem.__alloc_pages(__pages, __gfm);
    if (mblk == nullptr)
        return nullptr;

    // set paging in advance
    if (__gfm & _GFM_PRE_PAGING) {
        uint32_t nr_pages = mblk->nr_pages;

        for (int buddy_order = 10; buddy_order >= 0; buddy_order--) {
            uint32_t nr_pages_by_order = 1u << buddy_order;
            while (nr_pages >= nr_pages_by_order) {
                void *physical_addr = mm::__detail::alloc_physical_order(buddy_order, 0);
                if (physical_addr == nullptr) {
                    kvmem.__free((void *)mblk->base);
                    return nullptr;
                }

                auto pair = ktl::make_pair<ptr_t, uint32_t>(physical_addr, nr_pages_by_order);
                auto node = new ktl::intrusive_list_node<ktl::pair<void *, uint32_t>>(pair);
                if (node == nullptr) {
                    mm::__detail::free_physical_order(physical_addr);
                    kvmem.__free((void *)mblk->base);
                    return nullptr;
                }

                {
                    bigos::irq::InterruptGuard guard;
                    mblk->physical_area.insert(node);
                }
                nr_pages -= nr_pages_by_order;
            }
        }

        if (!kvmem.map_kernel_range(mblk)) {
            kvmem.__free((void *)mblk->base);
            return nullptr;
        }
    }

    return reinterpret_cast<void *>(mblk->base);
}

void free_pages(const void *__p) noexcept {
    kvmem.__free(__p);
}
NAMESPACE_BIGOS_END
