#include <arch/x86/boot/boot_info.h>
#include <bigos/io.h>   // remove later

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

static ktl::klist<bigos::mm::PageBlock *> gPageBlockList;

NAMESPACE_BIGOS_BEG
namespace mm {
    uint32_t g_nr_pages() noexcept {
        return gNrPages;
    }

    uint32_t g_nr_free_pages() noexcept {
        return gNrFreePages;
    }

    void Zone::merge(ktl::klist_node<PageBlock *> *__pblk_node) noexcept {
        PageBlock *pblk = **__pblk_node;
        if (pblk->order >= BUDDY_MAX_ORDER)
            return;

        uint64_t end_addr;
        PageBlock *adjacent_pblk = nullptr;
        ktl::klist_node<PageBlock *> *adjacent_pblk_node = nullptr;

        auto &ls = pblk->zone->free_area_[pblk->order];

        // try to merge with next one
        if (__pblk_node->next != nullptr) {
            adjacent_pblk_node = (ktl::klist_node<PageBlock *> *)__pblk_node->next;
            adjacent_pblk = **adjacent_pblk_node;

            end_addr = pblk->base + pblk->len;
            if (end_addr == adjacent_pblk->base) {
                auto iter = ktl::klist<PageBlock *>::iterator(__pblk_node);
                auto iter_next = ktl::klist<PageBlock *>::iterator(adjacent_pblk_node);

                ls.erase(iter);
                ls.erase(iter_next);

                pblk->len += adjacent_pblk->len;
                pblk->order++;

                delete adjacent_pblk;
                delete adjacent_pblk_node;

                pblk->zone->__base_free(__pblk_node);
                return;
            }
        }

        // try to merge with previous one
        if (__pblk_node->prev != nullptr) {
            adjacent_pblk_node = (ktl::klist_node<PageBlock *> *)__pblk_node->next;
            adjacent_pblk = **adjacent_pblk_node;

            end_addr = adjacent_pblk->base + adjacent_pblk->len;
            if (end_addr == pblk->base) {
                auto iter = ktl::klist<PageBlock *>::iterator(__pblk_node);
                auto iter_prev = ktl::klist<PageBlock *>::iterator(adjacent_pblk_node);

                ls.erase(iter);
                ls.erase(iter_prev);

                adjacent_pblk->len += pblk->len;
                adjacent_pblk->order++;

                delete pblk;
                delete __pblk_node;

                adjacent_pblk->zone->__base_free(adjacent_pblk_node);
                return;
            }
        }
    }

    void Zone::__base_free(ktl::klist_node<PageBlock *> *__pblk_node) noexcept {
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

    void Zone::__new_free(ktl::klist_node<PageBlock *> *__pblk_node) noexcept {
        __base_free(__pblk_node);

        auto pblk = **__pblk_node;
        uint32_t pages = 1ul << pblk->order;

        nr_pages_ += pages;
        nr_free_pages_ += pages;

        gNrPages += pages;
        gNrFreePages += pages;
    }

    void Zone::free(ktl::klist_node<PageBlock *> *__pblk_node) noexcept {
        __base_free(__pblk_node);

        auto pblk = **__pblk_node;
        uint32_t pages = 1ul << pblk->order;

        nr_free_pages_ += pages;

        gNrFreePages += pages;
    }

    ktl::klist_node<PageBlock *> *Zone::alloc(uint32_t __order) noexcept {
        uint32_t real_order = __order;
        while (real_order <= BUDDY_MAX_ORDER && free_area_[real_order].empty())
            real_order++;

        if (real_order > BUDDY_MAX_ORDER) {
            // TODO no enough pages to alloc
            return nullptr;
        }

        auto &ls = free_area_[real_order];
        auto iter = ls.begin();

        ls.erase(iter);

        auto pblk = *iter;
        if (real_order > __order) {
            // divide
            pblk->order = __order;
            pblk->len = get_pblk_size(__order);

            uint64_t base = pblk->base + pblk->len;
            uint64_t rest_len = ((1u << real_order) - (1u << __order)) * PAGE_SIZE;

            for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
                uint64_t temp_len = get_pblk_size(BUDDY_MAX_ORDER - i);
                while (rest_len >= temp_len) {
                    auto temp_pblk = new PageBlock();
                    auto pblk_node = new ktl::klist_node<PageBlock *>(temp_pblk);

                    temp_pblk->base = base;
                    temp_pblk->len = temp_len;
                    temp_pblk->order = BUDDY_MAX_ORDER - i;
                    temp_pblk->flags = pblk->flags;
                    temp_pblk->zone = this;

                    __base_free(pblk_node);

                    base += temp_len;
                    rest_len -= temp_len;
                }
            }
        }

        gPageBlockList.insert(iter._node);
        uint32_t pages = (1ul << ((*iter)->order));

        gNrFreePages -= pages;
        nr_free_pages_ -= pages;

        return static_cast<ktl::klist_node<PageBlock *> *>(iter._node);
    }

    namespace __detail {
        struct ARDS {
            uint64_t base;
            uint64_t len;
            uint32_t type;
            // ACPI 3.0 extended attribute bitfield
            uint32_t attributes;
        };

        static void handle_ards(uint64_t __base, uint64_t __len) noexcept {
            // base 4k alignment
            if (__base % PAGE_SIZE) {
                uint64_t aligned_base = (__base & 0xfffffffffffff000ul) + PAGE_SIZE;
                uint64_t drop_size = aligned_base - __base;

                __base = aligned_base;
                __len -= drop_size;
            }

            // len 4k alignment
            if (__len % PAGE_SIZE)
                __len = __len & 0xfffffffffffff000ul;

            uint64_t end_addr = __base + __len;

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
                    PageBlock *pblk = new PageBlock;
                    pblk->base = __base;
                    pblk->len = pblk_size;
                    pblk->flags = 0;
                    pblk->order = BUDDY_MAX_ORDER - i;
                    pblk->zone = zone;

                    auto node = new ktl::klist_node<PageBlock *>(pblk);
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

        void init_buddy() {
            const BootInfo *info = boot_info();
            if (info != nullptr)
                gKernelSize = (uint32_t)info->kernel_memory_size;
            else
                gKernelSize = *((uint32_t *)0x80c);
            gKernelEndAddr = ((gKernelSize + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE + KERNEL_BASE;

            uint32_t nr_ards = info != nullptr ? info->e820_entry_count : *(uint16_t *)0x800;
            ARDS *ards_arr = info != nullptr ? (ARDS *)info->e820_entry_address : (ARDS *)0x500;

            for (uint32_t i = 0; i < nr_ards; i++) {
                if (ards_arr[i].type == ARDS_USABLE)
                    handle_ards(ards_arr[i].base, ards_arr[i].len);
            }
        }

        void *alloc_physical_pages(uint32_t __order, gfm_t __gfm) noexcept {
            uint32_t nr_pages = 1u << __order;

            uint32_t index = ZONE_NORMAL;
            if ((__gfm & _GFM_ZONE_MASK) == _GFM_ZONE_DMA)
                index = ZONE_DMA;

            if ((__gfm & _GFM_ZONE_MASK) == _GFM_ZONE_DMA32)
                index = ZONE_DMA32;

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

        void free_physical_pages(const void *__p) noexcept {
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

            auto pblk_node = (ktl::klist_node<mm::PageBlock *> *)iter._node;
            auto zone = (**pblk_node)->zone;

            zone->free(pblk_node);
        }
    }   // namespace __detail

    void print_physical_memory_info() noexcept {
        kputs("buddy system info:\n");
        kputs("pblk  size: ");
        for (int i = 0; i <= BUDDY_MAX_ORDER; i++)
            kprintf("%d\t", 1u << i);
        kput('\n');

        kputs("pblk count: ");
        for (int order = 0; order <= BUDDY_MAX_ORDER; order++) {
            uint32_t temp = 0;
            for (int i = 0; i < 3; i++)
                temp += zone_arr[i]->nr_pblk_by_order(order);

            kprintf("%d\t", temp);
        }
        kput('\n');

        kprintf("free physical pages:%d\ntotal available physical pages:%d\n", gNrFreePages, gNrPages);
    }
}   // namespace mm
NAMESPACE_BIGOS_END
