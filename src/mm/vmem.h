#ifndef _BIG_VMEM_H
#define _BIG_VMEM_H

#include <ktl/list.h>
#include <ktl/pair.h>
#include <bigos/types.h>
#include <bigos/attributes.h>
#include <arch/x86/boot/boot_info.h>

NAMESPACE_BIGOS_BEG
void *alloc_kernel_pages(uint32_t __pages, gfm_t __gfm) noexcept;

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
        void init_direct_map(const BootInfoHeader *__boot_info);
        void init_vmem();
        // IRQ-disabled-only snapshot of the kernel heap/vmalloc free-page count.
        uint32_t kernel_vmem_free_pages() noexcept;
        bool clone_kernel_page_mapping_in_root(uint64_t __root_phys, uint64_t __vaddr) noexcept;
        bool user_range_writable(uint64_t __root_phys, uint64_t __vaddr, uint64_t __len) noexcept;
        bool copy_from_user_root(uint64_t __root_phys, uint64_t __addr, void *__dst, uint64_t __len) noexcept;
        bool copy_to_user_root(uint64_t __root_phys, uint64_t __addr, const void *__src, uint64_t __len) noexcept;
    }   // namespace __detail

    class VMem {
    private:
        friend void __detail::init_vmem();
        friend void *::bigos::alloc_kernel_pages(uint32_t __pages, gfm_t __gfm) noexcept;
        void merge(ktl::intrusive_list_node<MemoryBlock *> *__mblk_node) noexcept;
        _attr_nodiscard_ bool map_kernel_range(MemoryBlock *__mblk) noexcept;
        void rollback_kernel_range(uint64_t __base, uint32_t __nr_pages) noexcept;
        void unmap_kernel_range(MemoryBlock *__mblk) noexcept;
        void release_physical_area(MemoryBlock *__mblk) noexcept;

        pml4_t pml4_;
        ktl::intrusive_list<MemoryBlock *> free_area_;
        ktl::intrusive_list<MemoryBlock *> used_area_;

        uint32_t nr_pages_;
        uint32_t nr_free_pages_;

    public:
        inline uint32_t nr_free_pages() const noexcept {
            return nr_free_pages_;
        }

        // Internal KVMEM metadata paths; non-IRQ-handler-safe.
        void __free(const void *__p) noexcept;

        _attr_nodiscard_ MemoryBlock *__alloc_pages(uint32_t __pages, gfm_t __gfm) noexcept _attr_malloc_;
    };

}   // namespace mm
NAMESPACE_BIGOS_END

#endif   // _BIG_VMEM_H
