#include <arch/x86/boot/boot_info.h>
#include <bigos/io.h>   // remove later
#include <bigos/panic.h>
#include <irq/interrupt.h>

#include "buddy.h"

// ards types
#define ARDS_USABLE   1
#define ARDS_RESERVED 2
#define ARDS_ARM      3   // ACPI reclaimable memory
#define ARDS_ANM      4   // ACPI NVS memory
#define ARDS_BAD      5   // aera containing bad memory

// limits
#define KERNEL_BASE  0x1000000ul
#define DMA_LIMIT    0x1000000ul
#define DMA32_LIMIT  0x100000000ul
#define LOWEST_LIMIT 0x200000ul   // lowest 2MB reserved

// kernel size in bytes
static uint32_t gKernelSize;
// end physical address of kernel
static uint64_t gKernelEndAddr;

static uint32_t gNrPages;
static uint32_t gNrFreePages;

static bigos::mm::Zone zone_dma;
static bigos::mm::Zone zone_dma32;
static bigos::mm::Zone zone_normal;
static bigos::mm::Zone *zone_arr[] = {&zone_dma, &zone_dma32, &zone_normal};

#define ZONE_DMA    0
#define ZONE_DMA32  1
#define ZONE_NORMAL 2

static ktl::intrusive_list<bigos::mm::PageBlock *> gPageBlockList;

namespace {
    using PageBlockNode = ktl::intrusive_list_node<bigos::mm::PageBlock *>;

    constexpr uint32_t EARLY_METADATA_MAX_BOOT_MEMORY_REGIONS = 128;
    constexpr uint32_t EARLY_METADATA_REGION_SPLIT_BUDGET = 128;
    constexpr uint32_t EARLY_METADATA_PAGE_BLOCK_CAPACITY =
        EARLY_METADATA_MAX_BOOT_MEMORY_REGIONS * EARLY_METADATA_REGION_SPLIT_BUDGET;
    constexpr uint32_t EARLY_METADATA_LIST_NODE_CAPACITY = EARLY_METADATA_PAGE_BLOCK_CAPACITY;
    constexpr size_t EARLY_METADATA_ALIGNMENT = 16;

    constexpr size_t align_up_const(size_t value, size_t alignment) noexcept {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    constexpr size_t EARLY_METADATA_ARENA_BYTES =
        EARLY_METADATA_PAGE_BLOCK_CAPACITY * align_up_const(sizeof(bigos::mm::PageBlock), EARLY_METADATA_ALIGNMENT) +
        EARLY_METADATA_LIST_NODE_CAPACITY * align_up_const(sizeof(PageBlockNode), EARLY_METADATA_ALIGNMENT);

    static_assert(BIGOS_BOOT_INFO_V2_MAX_SIZE / sizeof(BootMemoryRegion) <= EARLY_METADATA_MAX_BOOT_MEMORY_REGIONS);
    static_assert(alignof(bigos::mm::PageBlock) <= EARLY_METADATA_ALIGNMENT);
    static_assert(alignof(PageBlockNode) <= EARLY_METADATA_ALIGNMENT);

    alignas(EARLY_METADATA_ALIGNMENT) static uint8_t gEarlyMetadataArenaStorage[EARLY_METADATA_ARENA_BYTES];

    class EarlyMetadataArena {
    private:
        uint8_t *base_;
        size_t capacity_;
        size_t used_;
        size_t high_water_;
        uint32_t page_blocks_used_;
        uint32_t list_nodes_used_;
        bool sealed_;

    public:
        void init(uint8_t *__base, size_t __capacity) noexcept {
            base_ = __base;
            capacity_ = __capacity;
            used_ = 0;
            high_water_ = 0;
            page_blocks_used_ = 0;
            list_nodes_used_ = 0;
            sealed_ = false;
        }

        void seal() noexcept {
            sealed_ = true;
        }

        void *alloc(size_t __size, size_t __alignment) noexcept {
            if (sealed_ || __alignment == 0)
                return nullptr;

            uint64_t base_addr = (uint64_t)base_;
            uint64_t current = base_addr + used_;
            uint64_t aligned = (current + __alignment - 1) & ~((uint64_t)__alignment - 1);
            size_t new_used = (size_t)(aligned - base_addr) + __size;
            if (new_used > capacity_)
                return nullptr;

            used_ = new_used;
            if (used_ > high_water_)
                high_water_ = used_;
            return (void *)aligned;
        }

        bigos::mm::PageBlock *alloc_page_block() noexcept {
            if (page_blocks_used_ >= EARLY_METADATA_PAGE_BLOCK_CAPACITY)
                return nullptr;

            void *storage = alloc(sizeof(bigos::mm::PageBlock), alignof(bigos::mm::PageBlock));
            if (storage == nullptr)
                return nullptr;

            page_blocks_used_++;
            return new (storage) bigos::mm::PageBlock();
        }

        PageBlockNode *alloc_page_block_node(bigos::mm::PageBlock *__pblk) noexcept {
            if (list_nodes_used_ >= EARLY_METADATA_LIST_NODE_CAPACITY)
                return nullptr;

            void *storage = alloc(sizeof(PageBlockNode), alignof(PageBlockNode));
            if (storage == nullptr)
                return nullptr;

            list_nodes_used_++;
            return new (storage) PageBlockNode(__pblk);
        }

        bool owns(const void *__p) const noexcept {
            uint64_t addr = (uint64_t)__p;
            uint64_t base_addr = (uint64_t)base_;
            return addr >= base_addr && addr < base_addr + capacity_;
        }

        uint32_t page_blocks_used() const noexcept {
            return page_blocks_used_;
        }

        uint32_t page_blocks_capacity() const noexcept {
            return EARLY_METADATA_PAGE_BLOCK_CAPACITY;
        }

        uint32_t list_nodes_used() const noexcept {
            return list_nodes_used_;
        }

        uint32_t list_nodes_capacity() const noexcept {
            return EARLY_METADATA_LIST_NODE_CAPACITY;
        }

        uint32_t used_bytes() const noexcept {
            return (uint32_t)used_;
        }

        uint32_t high_water_bytes() const noexcept {
            return (uint32_t)high_water_;
        }

        uint32_t capacity_bytes() const noexcept {
            return (uint32_t)capacity_;
        }
    };

    static EarlyMetadataArena gEarlyMetadataArena;

    static void destroy_buddy_metadata(bigos::mm::PageBlock *__pblk, PageBlockNode *__node) noexcept {
        if (__pblk != nullptr) {
            if (gEarlyMetadataArena.owns(__pblk)) {
                // Arena-backed PageBlock storage has static lifetime and is never returned to kmalloc.
                (void)__pblk;
            } else {
                delete __pblk;
            }
        }

        if (__node != nullptr) {
            if (gEarlyMetadataArena.owns(__node))
                __node->~PageBlockNode();
            else
                delete __node;
        }
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace mm {
    uint32_t g_nr_pages() noexcept {
        // Context-agnostic after memory initialization: total page count is not mutated at runtime.
        return gNrPages;
    }

    uint32_t g_nr_free_pages() noexcept {
        bigos::irq::InterruptGuard guard;
        return gNrFreePages;
    }

    void Zone::merge(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept {
        PageBlock *pblk = **__pblk_node;
        if (pblk->order >= BUDDY_MAX_ORDER)
            return;

        uint64_t end_addr;
        PageBlock *adjacent_pblk = nullptr;
        ktl::intrusive_list_node<PageBlock *> *adjacent_pblk_node = nullptr;

        auto &ls = pblk->zone->free_area_[pblk->order];

        auto iter = ktl::intrusive_list<PageBlock *>::iterator(__pblk_node);
        auto iter_next = iter;
        ++iter_next;

        // try to merge with next one
        if (iter_next != ls.end()) {
            adjacent_pblk_node = (ktl::intrusive_list_node<PageBlock *> *)iter_next._node;
            adjacent_pblk = *iter_next;
            end_addr = pblk->base + pblk->len;
            if (end_addr == adjacent_pblk->base) {
                ls.erase(iter);
                ls.erase(iter_next);

                pblk->len += adjacent_pblk->len;
                pblk->order++;

                destroy_buddy_metadata(adjacent_pblk, adjacent_pblk_node);

                pblk->zone->__base_free(__pblk_node);
                return;
            }
        }

        // try to merge with previous one
        iter = ktl::intrusive_list<PageBlock *>::iterator(__pblk_node);
        if (iter != ls.begin()) {
            auto iter_prev = iter;
            --iter_prev;
            adjacent_pblk_node = (ktl::intrusive_list_node<PageBlock *> *)iter_prev._node;
            adjacent_pblk = *iter_prev;

            end_addr = adjacent_pblk->base + adjacent_pblk->len;
            if (end_addr == pblk->base) {
                ls.erase(iter);
                ls.erase(iter_prev);

                adjacent_pblk->len += pblk->len;
                adjacent_pblk->order++;

                destroy_buddy_metadata(pblk, __pblk_node);

                adjacent_pblk->zone->__base_free(adjacent_pblk_node);
                return;
            }
        }
    }

    void Zone::__base_free(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept {
        if (__pblk_node == nullptr)
            return;

        auto pblk = **__pblk_node;

        if (pblk->order > BUDDY_MAX_ORDER)
            return;

        auto &ls = free_area_[pblk->order];
        auto iter = ls.begin();

        while (iter != ls.end()) {
            if ((*iter)->base > pblk->base)
                break;
            ++iter;
        }

        ls.insert(iter, __pblk_node);
        merge(__pblk_node);
    }

    void Zone::__new_free(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept {
        if (__pblk_node == nullptr)
            return;

        auto pblk = **__pblk_node;
        uint32_t pages = 1ul << pblk->order;

        __base_free(__pblk_node);

        nr_pages_ += pages;
        nr_free_pages_ += pages;

        gNrPages += pages;
        gNrFreePages += pages;
    }

    void Zone::free(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept {
        if (__pblk_node == nullptr)
            return;

        auto pblk = **__pblk_node;
        uint32_t pages = 1ul << pblk->order;

        __base_free(__pblk_node);

        nr_free_pages_ += pages;

        gNrFreePages += pages;
    }

    ktl::intrusive_list_node<PageBlock *> *Zone::alloc(uint32_t __order) noexcept {
        uint32_t real_order = __order;
        while (real_order <= BUDDY_MAX_ORDER && free_area_[real_order].empty())
            real_order++;

        if (real_order > BUDDY_MAX_ORDER) {
            // TODO no enough pages to alloc
            return nullptr;
        }

        auto &ls = free_area_[real_order];
        auto iter = ls.begin();
        auto pblk = *iter;
        auto pblk_node = static_cast<ktl::intrusive_list_node<PageBlock *> *>(iter._node);

        ls.erase(iter);

        if (real_order > __order) {
            uint64_t original_base = pblk->base;
            uint64_t original_len = pblk->len;
            uint32_t original_order = pblk->order;
            uint32_t original_flags = pblk->flags;
            Zone *original_zone = pblk->zone;
            PageBlock *split_pblks[BUDDY_MAX_ORDER + 1] = {};
            ktl::intrusive_list_node<PageBlock *> *split_nodes[BUDDY_MAX_ORDER + 1] = {};
            uint32_t split_count = 0;

            uint64_t alloc_len = get_pblk_size(__order);
            uint64_t base = pblk->base + alloc_len;
            uint64_t rest_len = ((1u << real_order) - (1u << __order)) * PAGE_SIZE;

            for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
                uint64_t temp_len = get_pblk_size(BUDDY_MAX_ORDER - i);
                while (rest_len >= temp_len) {
                    auto temp_pblk = new PageBlock();
                    if (temp_pblk == nullptr)
                        goto split_failed;

                    temp_pblk->base = base;
                    temp_pblk->len = temp_len;
                    temp_pblk->order = BUDDY_MAX_ORDER - i;
                    temp_pblk->flags = pblk->flags;
                    temp_pblk->zone = this;

                    auto temp_node = new ktl::intrusive_list_node<PageBlock *>(temp_pblk);
                    if (temp_node == nullptr) {
                        delete temp_pblk;
                        goto split_failed;
                    }

                    split_pblks[split_count] = temp_pblk;
                    split_nodes[split_count] = temp_node;
                    split_count++;

                    base += temp_len;
                    rest_len -= temp_len;
                }
            }

            pblk->order = __order;
            pblk->len = alloc_len;

            for (uint32_t i = 0; i < split_count; i++)
                __base_free(split_nodes[i]);

            goto split_done;

        split_failed:
            for (uint32_t i = 0; i < split_count; i++) {
                delete split_nodes[i];
                delete split_pblks[i];
            }

            pblk->base = original_base;
            pblk->len = original_len;
            pblk->order = original_order;
            pblk->flags = original_flags;
            pblk->zone = original_zone;

            auto insert_position = free_area_[original_order].begin();
            while (insert_position != free_area_[original_order].end()) {
                if ((*insert_position)->base > pblk->base)
                    break;
                insert_position++;
            }
            free_area_[original_order].insert(insert_position, pblk_node);
            return nullptr;
        }

    split_done:
        gPageBlockList.insert(pblk_node);
        uint32_t pages = (1ul << pblk->order);

        gNrFreePages -= pages;
        nr_free_pages_ -= pages;

        return pblk_node;
    }

    namespace __detail {
        struct ARDS {
            uint64_t base;
            uint64_t len;
            uint32_t type;
            // ACPI 3.0 extended attribute bitfield
            uint32_t attributes;
        };

        static uint32_t normalize_ards_type(uint32_t type) noexcept {
            switch (type) {
                case ARDS_USABLE:
                    return BIGOS_BOOT_MEMORY_TYPE_USABLE;
                case ARDS_RESERVED:
                    return BIGOS_BOOT_MEMORY_TYPE_RESERVED;
                case ARDS_ARM:
                    return BIGOS_BOOT_MEMORY_TYPE_ACPI_RECLAIM;
                case ARDS_ANM:
                    return BIGOS_BOOT_MEMORY_TYPE_ACPI_NVS;
                case ARDS_BAD:
                    return BIGOS_BOOT_MEMORY_TYPE_BAD_MEMORY;
                default:
                    return BIGOS_BOOT_MEMORY_TYPE_RESERVED;
            }
        }

        static uint64_t ards_attributes(const ARDS &ards) noexcept {
            return ((uint64_t)ards.type << BIGOS_BOOT_MEMORY_ATTR_FIRMWARE_TYPE_SHIFT) |
                   (ards.attributes & 0x1u ? BIGOS_BOOT_MEMORY_ATTR_WRITE_BACK : 0);
        }

        static void halt_memory_handoff_failed() noexcept {
            bigos::kpanic(bigos::PanicCode::BuddyMemoryHandoffFailed, "mm-buddy", "invalid boot memory map\n");
        }

        static void halt_early_metadata_exhausted(const char *__kind) noexcept {
            bigos::kpanic(bigos::PanicCode::EarlyMetadataArenaExhausted, "mm-arena",
                "early memory metadata arena exhausted while allocating %s\n"
                "page blocks:%d/%d list nodes:%d/%d\n"
                "arena bytes:%d/%d high-water:%d\n",
                __kind, gEarlyMetadataArena.page_blocks_used(), gEarlyMetadataArena.page_blocks_capacity(),
                gEarlyMetadataArena.list_nodes_used(), gEarlyMetadataArena.list_nodes_capacity(),
                gEarlyMetadataArena.used_bytes(), gEarlyMetadataArena.capacity_bytes(),
                gEarlyMetadataArena.high_water_bytes());
        }

        static void init_early_metadata_arena() noexcept {
            gEarlyMetadataArena.init(gEarlyMetadataArenaStorage, sizeof(gEarlyMetadataArenaStorage));
        }

        static void seal_early_metadata_arena() noexcept {
            gEarlyMetadataArena.seal();
        }

        static PageBlock *new_bootstrap_page_block() noexcept {
            PageBlock *pblk = gEarlyMetadataArena.alloc_page_block();
            if (pblk == nullptr)
                halt_early_metadata_exhausted("PageBlock");
            return pblk;
        }

        static ktl::intrusive_list_node<PageBlock *> *new_bootstrap_page_block_node(PageBlock *__pblk) noexcept {
            auto node = gEarlyMetadataArena.alloc_page_block_node(__pblk);
            if (node == nullptr) {
                destroy_buddy_metadata(__pblk, nullptr);
                halt_early_metadata_exhausted("PageBlock list node");
            }
            return node;
        }

        static void handle_ards(uint64_t __base, uint64_t __len) noexcept {
            // base 4k alignment
            if (__base % PAGE_SIZE) {
                uint64_t aligned_base = (__base & 0xfffffffffffff000ul) + PAGE_SIZE;
                uint64_t drop_size = aligned_base - __base;
                if (__len <= drop_size)
                    return;

                __base = aligned_base;
                __len -= drop_size;
            }

            // len 4k alignment
            if (__len % PAGE_SIZE)
                __len = __len & 0xfffffffffffff000ul;

            uint64_t end_addr = __base + __len;
            if (end_addr <= __base)
                return;

            // lowest memory are reserved
            if (__base < LOWEST_LIMIT) {
                if (end_addr > LOWEST_LIMIT)
                    handle_ards(LOWEST_LIMIT, end_addr - LOWEST_LIMIT);
                return;
            }

            // kernel reserved
            if (__base < KERNEL_BASE) {
                if (end_addr > KERNEL_BASE) {
                    handle_ards(__base, KERNEL_BASE - __base);
                    if (end_addr > gKernelEndAddr)
                        handle_ards(gKernelEndAddr, end_addr - gKernelEndAddr);
                    return;
                }
            } else if (__base < gKernelEndAddr && end_addr > gKernelEndAddr) {
                handle_ards(gKernelEndAddr, end_addr - gKernelEndAddr);
                return;
            }

            Zone *zone;
            if (__base < DMA_LIMIT) {
                zone = &zone_dma;
                if (end_addr > DMA_LIMIT) {
                    __len = DMA_LIMIT - __base;
                    handle_ards(DMA_LIMIT, end_addr - DMA_LIMIT);
                }
            } else if (__base < DMA32_LIMIT) {
                zone = &zone_dma32;
                if (end_addr > DMA32_LIMIT) {
                    __len = DMA32_LIMIT - __base;
                    handle_ards(DMA32_LIMIT, end_addr - DMA32_LIMIT);
                }
            } else {
                zone = &zone_normal;
            }

            for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
                uint64_t pblk_size = get_pblk_size(BUDDY_MAX_ORDER - i);
                while (__len >= pblk_size) {
                    PageBlock *pblk = new_bootstrap_page_block();
                    pblk->base = __base;
                    pblk->len = pblk_size;
                    pblk->flags = 0;
                    pblk->order = BUDDY_MAX_ORDER - i;
                    pblk->zone = zone;

                    auto node = new_bootstrap_page_block_node(pblk);
                    zone->__new_free(node);

                    __base += pblk_size;
                    __len -= pblk_size;
                }
            }
        }

        const BootInfo *boot_info() noexcept {
            const BootInfo *info = (const BootInfo *)BIGOS_BOOT_INFO_ADDRESS;
            if (info->magic != BIGOS_BOOT_INFO_MAGIC)
                return nullptr;
            if (info->version != BIGOS_BOOT_INFO_VERSION)
                return nullptr;
            if (info->size < BIGOS_BOOT_INFO_SIZE)
                return nullptr;
            return info;
        }

        static const BootInfoCore *boot_info_core(const BootInfoHeader *header) noexcept {
            const BootInfoSection *section = bigos_boot_info_v2_find_section(header, BIGOS_BOOT_SECTION_TYPE_CORE);
            if (section == nullptr || section->size < sizeof(BootInfoCore))
                return nullptr;
            return (const BootInfoCore *)((const uint8_t *)header + section->offset);
        }

        static bool consume_region(const BootMemoryRegion &region) noexcept {
            if (region.normalized_type != BIGOS_BOOT_MEMORY_TYPE_USABLE)
                return false;
            handle_ards(region.physical_base, region.length);
            return true;
        }

        static uint32_t consume_v2_memory_map(const BootInfoHeader *header) noexcept {
            const BootInfoSection *section =
                bigos_boot_info_v2_find_section(header, BIGOS_BOOT_SECTION_TYPE_MEMORY_MAP);
            if (section == nullptr || section->size % sizeof(BootMemoryRegion) != 0)
                return 0;

            const BootMemoryRegion *regions = (const BootMemoryRegion *)((const uint8_t *)header + section->offset);
            uint32_t usable_regions = 0;
            uint32_t nr_regions = section->size / sizeof(BootMemoryRegion);
            for (uint32_t i = 0; i < nr_regions; i++) {
                if (consume_region(regions[i]))
                    usable_regions++;
            }
            return usable_regions;
        }

        static uint32_t consume_v1_memory_map(const BootInfo *info) noexcept {
            ARDS *ards_arr = (ARDS *)info->e820_entry_address;
            uint32_t usable_regions = 0;
            for (uint32_t i = 0; i < info->e820_entry_count; i++) {
                BootMemoryRegion region = {
                    ards_arr[i].base,
                    ards_arr[i].len,
                    normalize_ards_type(ards_arr[i].type),
                    BIGOS_BOOT_MEMORY_SOURCE_BIOS_E820,
                    ards_attributes(ards_arr[i]),
                    ards_arr[i].type,
                };
                if (consume_region(region))
                    usable_regions++;
            }
            return usable_regions;
        }

        void init_buddy(const BootInfoHeader *__boot_info) {
            BootHandoff handoff = bigos_boot_resolve_handoff(__boot_info);
            uint32_t usable_regions = 0;

            init_early_metadata_arena();

            if (handoff.v2 != nullptr) {
                const BootInfoCore *core = boot_info_core(handoff.v2);
                if (core == nullptr)
                    halt_memory_handoff_failed();
                gKernelSize = (uint32_t)core->kernel_memory_size;
                gKernelEndAddr = ((gKernelSize + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE + KERNEL_BASE;
                usable_regions = consume_v2_memory_map(handoff.v2);
            } else if (handoff.v1 != nullptr) {
                gKernelSize = (uint32_t)handoff.v1->kernel_memory_size;
                gKernelEndAddr = ((gKernelSize + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE + KERNEL_BASE;
                usable_regions = consume_v1_memory_map(handoff.v1);
            } else {
                halt_memory_handoff_failed();
            }

            if (usable_regions == 0)
                halt_memory_handoff_failed();

            seal_early_metadata_arena();
        }

        void *alloc_physical_order(uint32_t __order, gfm_t __gfm) noexcept {
            if (__order > BUDDY_MAX_ORDER)
                return nullptr;

            uint32_t nr_pages = 1u << __order;

            uint32_t index = ZONE_NORMAL;
            if ((__gfm & _GFM_ZONE_MASK) == _GFM_ZONE_DMA)
                index = ZONE_DMA;

            if ((__gfm & _GFM_ZONE_MASK) == _GFM_ZONE_DMA32)
                index = ZONE_DMA32;

            bigos::irq::InterruptGuard guard;
            while (zone_arr[index]->nr_free_pages() < nr_pages) {
                if (index > 0)
                    index--;
                else {
                    // TODO no enough pages to alloc
                    return nullptr;
                }
            }

            auto pblk_node = zone_arr[index]->alloc(__order);

            while (pblk_node == nullptr) {
                if (index > 0) {
                    index--;
                    pblk_node = zone_arr[index]->alloc(__order);
                } else {
                    // TODO no enough pages to alloc
                    return nullptr;
                }
            }

            auto pblk = **pblk_node;
            void *ret = (void *)pblk->base;

            return ret;
        }

        void free_physical_order(const void *__p) noexcept {
            if (__p == nullptr)
                return;

            bigos::irq::InterruptGuard guard;
            uint64_t base = (uint64_t)__p;

            auto iter = gPageBlockList.begin();
            auto end = gPageBlockList.end();

            while (iter != end) {
                if ((*iter)->base == base)
                    break;
                iter++;
            }

            if (iter == end)
                return;

            gPageBlockList.erase(iter);

            auto pblk_node = (ktl::intrusive_list_node<mm::PageBlock *> *)iter._node;
            auto zone = (**pblk_node)->zone;

            zone->free(pblk_node);
        }

    }   // namespace __detail

    void print_physical_memory_info() noexcept {
        uint32_t pblk_count[BUDDY_MAX_ORDER + 1] = {};
        uint32_t free_pages = 0;
        uint32_t total_pages = 0;

        {
            bigos::irq::InterruptGuard guard;
            for (int order = 0; order <= BUDDY_MAX_ORDER; order++) {
                for (int i = 0; i < 3; i++)
                    pblk_count[order] += zone_arr[i]->nr_pblk_by_order(order);
            }
            free_pages = gNrFreePages;
            total_pages = gNrPages;
        }

        kputs("buddy system info:\n");
        kputs("pblk  size: ");
        for (int i = 0; i <= BUDDY_MAX_ORDER; i++)
            kprintf("%d\t", 1u << i);
        kput('\n');

        kputs("pblk count: ");
        for (int order = 0; order <= BUDDY_MAX_ORDER; order++)
            kprintf("%d\t", pblk_count[order]);
        kput('\n');

        kprintf("free physical pages:%d\ntotal available physical pages:%d\n", free_pages, total_pages);
    }
}   // namespace mm
NAMESPACE_BIGOS_END
