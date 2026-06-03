#include <string.h>
#include <bigos/io.h>   //TODO remove later

#include "vmem.h"
#include "buddy.h"
#include "memdef.h"
#include <bigos/memory.h>

#define KVMEM_LEN        0x10000000000ul
#define KVMEM_BASE       0xffff880000000000ul
#define KERNEL_PML4_ADDR 0x2000ul

#define DEFAULT_ATTR_PML4E 0x0000000000000003ul
#define DEFAULT_ATTR_PDPTE 0x0000000000000003ul
#define DEFAULT_ATTR_PDE   0x0000000000000003ul
#define DEFAULT_ATTR_PTE   0x0000000000000003ul

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

NAMESPACE_BIGOS_BEG
namespace mm {
    namespace __detail {
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
