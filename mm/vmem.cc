#include <string.h>
#include <bigos/io.h>   //TODO remove later

#include "vmem.h"

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

#define INDEX_PML4_MASK 0x0000ff8000000000ul
#define INDEX_PDPT_MASK 0x0000007fc0000000ul
#define INDEX_PD_MASK   0x000000003fe00000ul
#define INDEX_PT_MASK   0x00000000001f0000ul

#define get_pml4_index(ADDR) ((ADDR & INDEX_PML4_MASK) >> INDEX_PML4_OFFSET)
#define get_pdpt_index(ADDR) ((ADDR & INDEX_PDPT_MASK) >> INDEX_PDPT_OFFSET)
#define get_pd_index(ADDR)   ((ADDR & INDEX_PD_MASK) >> INDEX_PD_OFFSET)
#define get_pt_index(ADDR)   ((ADDR & INDEX_PT_MASK) >> INDEX_PT_OFFSET)

#define self_mapping_pml4(ADDR) ((uint64_t *)0xffff804020100000ul)
#define self_mapping_pdpt(ADDR) ((uint64_t *)((get_pml4_index(ADDR) << 12) | 0xffff804020000000ul))
#define self_mapping_pd(ADDR)                                                                                          \
    ((uint64_t *)((get_pml4_index(ADDR) << 21) | (get_pdpt_index(ADDR) << 12) | 0xffff804000000000ul))
#define self_mapping_pt(ADDR)                                                                                          \
    ((uint64_t *)((get_pml4_index(ADDR) << 30) | (get_pdpt_index(ADDR) << 21) | (get_pd_index(ADDR) << 12) |           \
                  0xffff800000000000ul))

#define PAGING_DESCRIPTOR_ADDR_MASK 0x000ffffffffff000ul

static bigos::mm::VMem kvmem;

NAMESPACE_BIGOS_BEG
namespace mm {
    namespace __detail {
        void init_vmem() {
            MemoryBlock *mblk = new MemoryBlock();
            // mblk->vmem = &kvmem;
            mblk->base = KVMEM_BASE;
            // mblk->len = KVMEM_LEN;
            mblk->nr_pages = KVMEM_LEN / PAGE_SIZE;
            mblk->flags = 0;

            auto node = new ktl::klist_node<MemoryBlock *>(mblk);

            kvmem.pml4_ = (pml4_t)KERNEL_PML4_ADDR;
            kvmem.free_area_.insert(node);
            kvmem.nr_pages_ = mblk->nr_pages;
            kvmem.nr_free_pages_ = mblk->nr_pages;
        }
    }   // namespace __detail

    void VMem::set_paging(MemoryBlock *__mblk) {
        uint64_t base = __mblk->base;

        uint16_t i_pml4 = get_pml4_index(base);
        uint16_t i_pdpt = get_pdpt_index(base);
        uint16_t i_pd = get_pd_index(base);
        uint16_t i_pt = get_pt_index(base);

        for (auto physical_m : __mblk->physical_area) {
            uint32_t nr_physical_pages = physical_m.second;
            uint64_t physical_base = (uint64_t)physical_m.first;

            while (nr_physical_pages) {
                if (i_pt >= 512) {
                    i_pt = 0;
                    i_pd++;
                    if (i_pd >= 512) {
                        i_pd = 0;
                        i_pdpt++;
                        if (i_pdpt >= 512) {
                            i_pdpt = 0;
                            i_pml4++;
                            if (i_pml4 >= 512) {
                                // TODO out of range
                            }
                        }
                    }
                }

                uint64_t *entry;
                uint64_t page, paging_descriptor;

                // check if pdpt is valid
                entry = self_mapping_pml4(base);
                paging_descriptor = entry[i_pml4];
                if (!paging_descriptor) {
                    page = (uint64_t)__detail::alloc_physical_pages(0, 0);
                    self_mapping_pml4(base)[i_pml4] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PML4E;

                    entry = self_mapping_pdpt(base);
                    memset((void *)entry, 0, PAGE_SIZE);
                }

                // check if pd is valid
                entry = self_mapping_pdpt(base);
                paging_descriptor = entry[i_pdpt];
                if (!paging_descriptor) {
                    page = (uint64_t)__detail::alloc_physical_pages(0, 0);
                    self_mapping_pdpt(base)[i_pdpt] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PDPTE;

                    entry = self_mapping_pd(base);
                    memset((void *)entry, 0, PAGE_SIZE);
                }

                // check if pt is valid
                entry = self_mapping_pd(base);
                paging_descriptor = entry[i_pd];
                if (!paging_descriptor) {
                    page = (uint64_t)__detail::alloc_physical_pages(0, 0);
                    self_mapping_pd(base)[i_pd] = (page & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PDE;

                    entry = self_mapping_pt(base);
                    memset((void *)entry, 0, PAGE_SIZE);
                }

                // set paging
                self_mapping_pt(base)[i_pt] = (physical_base & PAGING_DESCRIPTOR_ADDR_MASK) | DEFAULT_ATTR_PTE;

                i_pt++;
                nr_physical_pages--;

                base += PAGE_SIZE;
                physical_base += PAGE_SIZE;
            }
        }
    }

    void VMem::merge(ktl::klist_node<MemoryBlock *> *__mblk_node) {
        MemoryBlock *mblk = **__mblk_node;

        uint64_t end_addr;
        MemoryBlock *adjacent_mblk = nullptr;
        ktl::klist_node<MemoryBlock *> *adjacent_mblk_node = nullptr;

        // try to merge with next one
        if (__mblk_node->next != nullptr) {
            adjacent_mblk_node = (ktl::klist_node<MemoryBlock *> *)__mblk_node->next;
            adjacent_mblk = **adjacent_mblk_node;

            end_addr = mblk->base + mblk->nr_pages * PAGE_SIZE;
            if (end_addr == adjacent_mblk->base) {
                auto iter = ktl::klist<MemoryBlock *>::iterator(__mblk_node);
                auto iter_next = ktl::klist<MemoryBlock *>::iterator(adjacent_mblk_node);

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
        if (__mblk_node->prev != nullptr) {
            adjacent_mblk_node = (ktl::klist_node<MemoryBlock *> *)__mblk_node->prev;
            adjacent_mblk = **adjacent_mblk_node;

            end_addr = adjacent_mblk->base + adjacent_mblk->nr_pages * PAGE_SIZE;
            if (end_addr == mblk->base) {
                auto iter = ktl::klist<MemoryBlock *>::iterator(__mblk_node);
                auto iter_prev = ktl::klist<MemoryBlock *>::iterator(adjacent_mblk_node);

                free_area_.erase(iter);
                free_area_.erase(iter_prev);

                adjacent_mblk->nr_pages += mblk->nr_pages;

                delete mblk;
                delete __mblk_node;

                iter = free_area_.begin();
                while (iter != free_area_.end()) {
                    if ((*iter)->base > mblk->base)
                        break;
                    iter++;
                }

                free_area_.insert(iter, adjacent_mblk_node);
                merge(adjacent_mblk_node);
                return;
            }
        }
    }

    void VMem::__free(const void *__p) {
        uint64_t addr = (uint64_t)__p;

        auto iter = used_area_.begin();
        while (iter != used_area_.end()) {
            if ((*iter)->base == addr)
                break;
            iter++;
        }

        if (iter == used_area_.end())
            return;
        used_area_.erase(iter);

        // free physical pages
        auto mblk = *iter;
        for (auto physical_pair : mblk->physical_area) {
            __detail::free_physical_pages(physical_pair.first);
        }

        // empty phsical_area
        while (!mblk->physical_area.empty()) {
            auto temp = mblk->physical_area.begin();
            mblk->physical_area.erase(temp);
            delete temp._node;
        }

        auto insert_position = free_area_.begin();
        while (insert_position != free_area_.end()) {
            if ((*insert_position)->base > mblk->base)
                break;
            insert_position++;
        }

        free_area_.insert(insert_position, iter._node);
        merge((ktl::klist_node<MemoryBlock *> *)iter._node);
    }

    MemoryBlock *VMem::__alloc_pages(uint32_t __pages, gfm_t __gfm) {
        if (nr_free_pages_ < __pages)
            return nullptr;

        auto iter = free_area_.begin();
        free_area_.erase(iter);

        auto mblk = *iter;

        // divide
        if (mblk->nr_pages > __pages) {
            auto new_mblk = new MemoryBlock();
            // new_mblk->vmem = this;
            new_mblk->base = mblk->base + PAGE_SIZE * __pages;
            new_mblk->flags = mblk->flags;
            new_mblk->nr_pages = mblk->nr_pages - __pages;

            mblk->nr_pages = __pages;

            auto node = new ktl::klist_node<MemoryBlock *>(new_mblk);
            free_area_.insert(node);
        }

        used_area_.insert(iter._node);
        return mblk;
    }
}   // namespace mm

// defined in memory.h
void *alloc_pages(uint32_t __pages, gfm_t __gfm) {
    mm::MemoryBlock *mblk = kvmem.__alloc_pages(__pages, __gfm);

    // set paging in advance
    if (__gfm & _GFM_PRE_PAGING) {
        uint32_t nr_pages = mblk->nr_pages;

        for (int buddy_order = 10; buddy_order >= 0; buddy_order--) {
            uint32_t nr_pages_by_order = 1u << buddy_order;
            while (nr_pages >= nr_pages_by_order) {
                void *physical_addr = mm::__detail::alloc_physical_pages(buddy_order, 0);

                auto pair = ktl::make_pair<ptr_t, uint32_t>(physical_addr, nr_pages_by_order);
                auto node = new ktl::klist_node<ktl::pair<void *, uint32_t>>(pair);

                mblk->physical_area.insert(node);
                nr_pages -= nr_pages_by_order;
            }
        }

        kvmem.set_paging(mblk);
    }

    return reinterpret_cast<void *>(mblk->base);
}

void free_pages(const void *__p) {
    kvmem.__free(__p);
}
NAMESPACE_BIGOS_END
