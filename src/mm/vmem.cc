#include <string.h>
#include <bigos/io.h>   //TODO remove later
#include <bigos/panic.h>

#include "vmem.h"
#include "buddy.h"
#include "memdef.h"
#include <bigos/memory.h>

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

static_assert(bigos::mm::KDIRECT_BASE >= SELF_MAPPING_BASE + SELF_MAPPING_LEN);
static_assert(bigos::mm::KDIRECT_BASE >= KVMEM_BASE + KVMEM_LEN);
static_assert(bigos::mm::KDIRECT_BASE + bigos::mm::KDIRECT_LEN <= KERNEL_HIGHER_HALF_BASE);
static_assert(KVMEM_BASE < bigos::mm::KDIRECT_BASE || KVMEM_BASE >= bigos::mm::KDIRECT_BASE + bigos::mm::KDIRECT_LEN);

static inline bool paging_present(uint64_t __descriptor) noexcept {
    return (__descriptor & 0x1ul) != 0;
}

static inline void flush_kernel_tlb_page(uint64_t __vaddr) noexcept {
    asm volatile("invlpg (%0)" ::"r"(__vaddr) : "memory");
}

struct PagingDescriptorChange {
    uint64_t *entry;
    uint16_t index;
};

struct DirectMapRange {
    uint64_t base;
    uint64_t len;
};

constexpr uint32_t DIRECT_MAP_MAX_RANGES = 128;
static DirectMapRange gDirectMapRanges[DIRECT_MAP_MAX_RANGES];
static uint32_t gDirectMapRangeCount;

static void clear_new_paging_descriptors(PagingDescriptorChange *__changes, uint32_t __count) noexcept {
    while (__count) {
        __count--;
        __changes[__count].entry[__changes[__count].index] = 0;
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

static void direct_map_panic(const char *__message) noexcept {
    bigos::serial_init();
    bigos::kpanic(bigos::PanicCode::DirectMapInitFailed, "mm-direct-map", "%s\n", __message);
}

static bool direct_map_select_range(uint64_t __base, uint64_t __len, uint64_t *__out_base, uint64_t *__out_len) noexcept {
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
    if (paging_present(__entry[__index]))
        return true;

    uint64_t page = (uint64_t)bigos::mm::__detail::alloc_physical_order(0, 0);
    if (page == 0)
        return false;

    __changes[(*__change_count)++] = {__entry, __index};
    __entry[__index] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | __attr;
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
            flush_kernel_tlb_page(vaddr);
            uint64_t pages_to_next_pd = 1u << (INDEX_PD_OFFSET - INDEX_PT_OFFSET);
            i += pages_to_next_pd - (i % pages_to_next_pd);
            continue;
        }

        uint64_t *pte = &self_mapping_pt(vaddr)[get_pt_index(vaddr)];
        if (paging_present(*pte)) {
            *pte = 0;
            flush_kernel_tlb_page(vaddr);
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
            return kvmem.nr_free_pages();
        }
    }   // namespace __detail

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

    void VMem::rollback_kernel_range(uint64_t __base, uint32_t __nr_pages) noexcept {
        for (uint32_t i = 0; i < __nr_pages; i++) {
            uint64_t vaddr = __base + i * PAGE_SIZE;
            uint64_t *pte = mapped_pte(vaddr);
            if (pte != nullptr && paging_present(*pte)) {
                *pte = 0;
                flush_kernel_tlb_page(vaddr);
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
                uint16_t i_pml4 = get_pml4_index(base);
                uint16_t i_pdpt = get_pdpt_index(base);
                uint16_t i_pd = get_pd_index(base);
                uint16_t i_pt = get_pt_index(base);

                uint64_t *entry;
                uint64_t page, paging_descriptor;
                PagingDescriptorChange new_descriptors[3] = {};
                uint32_t new_descriptor_count = 0;

                // check if pdpt is valid
                entry = self_mapping_pml4(base);
                paging_descriptor = entry[i_pml4];
                if (!paging_descriptor) {
                    page = (uint64_t)__detail::alloc_physical_order(0, 0);
                    if (page == 0) {
                        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
                        rollback_kernel_range(__mblk->base, mapped_pages);
                        return false;
                    }
                    new_descriptors[new_descriptor_count++] = {entry, i_pml4};
                    self_mapping_pml4(base)[i_pml4] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PML4E;

                    entry = self_mapping_pdpt(base);
                    memset((void *)entry, 0, PAGE_SIZE);
                }

                // check if pd is valid
                entry = self_mapping_pdpt(base);
                paging_descriptor = entry[i_pdpt];
                if (!paging_descriptor) {
                    page = (uint64_t)__detail::alloc_physical_order(0, 0);
                    if (page == 0) {
                        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
                        rollback_kernel_range(__mblk->base, mapped_pages);
                        return false;
                    }
                    new_descriptors[new_descriptor_count++] = {entry, i_pdpt};
                    self_mapping_pdpt(base)[i_pdpt] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PDPTE;

                    entry = self_mapping_pd(base);
                    memset((void *)entry, 0, PAGE_SIZE);
                }

                // check if pt is valid
                entry = self_mapping_pd(base);
                paging_descriptor = entry[i_pd];
                if (!paging_descriptor) {
                    page = (uint64_t)__detail::alloc_physical_order(0, 0);
                    if (page == 0) {
                        clear_new_paging_descriptors(new_descriptors, new_descriptor_count);
                        rollback_kernel_range(__mblk->base, mapped_pages);
                        return false;
                    }
                    new_descriptors[new_descriptor_count++] = {entry, i_pd};
                    self_mapping_pd(base)[i_pd] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PDE;

                    entry = self_mapping_pt(base);
                    memset((void *)entry, 0, PAGE_SIZE);
                }

                // set paging
                self_mapping_pt(base)[i_pt] = (physical_base & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PTE;

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

                mblk->physical_area.insert(node);
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
