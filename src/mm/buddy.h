#ifndef _BIG_BUDDY_H
#define _BIG_BUDDY_H

#include <ktl/list.h>
#include <bigos/types.h>

#include "memdef.h"

// get page block size by order
#define get_pblk_size(ORDER) (PAGE_SIZE * (1ul << (ORDER)))
// page block struct size
#define PAGE_BLOCK_SIZE sizeof(bigos::mm::PageBlock)

NAMESPACE_BIGOS_BEG
namespace mm {
    class Zone;
    struct PageBlock {
        Zone *zone;
        uint64_t base;
        uint64_t len;
        uint32_t flags;
        uint32_t order;
    };

    class Zone {
    private:
        friend void handle_ards(uint64_t __base, uint64_t __len) noexcept;
        void merge(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept;

        ktl::intrusive_list<PageBlock *> free_area_[BUDDY_MAX_ORDER + 1];

        uint32_t nr_pages_;
        uint32_t nr_free_pages_;

    public:
        inline uint32_t nr_pages() noexcept _attr_pure_ {
            return nr_pages_;
        }
        inline uint32_t nr_free_pages() noexcept _attr_pure_ {
            return nr_free_pages_;
        }

        inline uint32_t nr_pblk_by_order(uint32_t __order) noexcept _attr_pure_ {
            return free_area_[__order].size();
        }

        void __base_free(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept;
        void __new_free(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept;
        void free(ktl::intrusive_list_node<PageBlock *> *__pblk_node) noexcept;

        ktl::intrusive_list_node<PageBlock *> *alloc(uint32_t __order) noexcept;
    };

    // total number of physical pages
    uint32_t g_nr_pages() noexcept;
    // total number of pysical pages which are unused
    uint32_t g_nr_free_pages() noexcept;

    namespace __detail {
        void init_buddy();

        void free_physical_pages(const void *__p) noexcept;
        _attr_nodiscard_ void *alloc_physical_pages(uint32_t __order, gfm_t __gfm) noexcept _attr_malloc_;
    }   // namespace __detail

    void print_physical_memory_info() noexcept;
}   // namespace mm
NAMESPACE_BIGOS_END
#endif   // _BIG_BUDDY_H
