#ifndef _BIG_VMEM_H
#define _BIG_VMEM_H

#include <ktl/list.h>
#include <ktl/pair.h>
#include <bigos/types.h>
#include <bigos/attributes.h>

NAMESPACE_BIGOS_BEG
namespace mm {
    typedef uint64_t *pt_t;
    typedef pt_t *pd_t;
    typedef pd_t *pdpt_t;
    typedef pdpt_t *pml4_t;

    // class VMem;
    struct MemoryBlock {
        // VMem* vmem;
        uint64_t base;
        // uint64_t len;
        uint32_t flags;
        uint32_t nr_pages;
        ktl::intrusive_list<ktl::pair<ptr_t, uint32_t>> physical_area;
    };

    namespace __detail {
        void init_vmem();
    }   // namespace __detail

    class VMem {
    private:
        friend void __detail::init_vmem();
        void merge(ktl::intrusive_list_node<MemoryBlock *> *__mblk_node) noexcept;

        pml4_t pml4_;
        ktl::intrusive_list<MemoryBlock *> free_area_;
        ktl::intrusive_list<MemoryBlock *> used_area_;

        uint32_t nr_pages_;
        uint32_t nr_free_pages_;

    public:
        void set_paging(MemoryBlock *__mblk) noexcept;

        void __free(const void *__p) noexcept;

        _attr_nodiscard_ MemoryBlock *__alloc_pages(uint32_t __pages, gfm_t __gfm) noexcept _attr_malloc_;
    };

}   // namespace mm
NAMESPACE_BIGOS_END

#endif   // _BIG_VMEM_H
