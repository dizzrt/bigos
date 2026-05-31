#include <new>
#include <stdarg.h>
#include <bigos/io.h>   // TODO remove later

#include "slab.h"
#include "buddy.h"
#include "memory.h"
#include "memdef.h"

NAMESPACE_BIGOS_BEG
namespace mm {
    // slab
    Slab::Slab(ptr_t __heap, uint32_t __flags, uint32_t __nr_objs, uint32_t __chunk_size, Cache *__belong_cache,
        ptr8_t __bp_heap)
        : bitset(__nr_objs, __bp_heap),
          base_((uint64_t)__heap),
          flags_(__flags),
          chunk_size_(__chunk_size),
          belong_cache_(__belong_cache) {}

    void *Slab::alloc_obj(gfm_t __gfm) noexcept {
        uint64_t offset = scan(1);
        if (offset == ktl::bitset::npos)
            return nullptr;
        if (set(offset) != 1)
            return nullptr;

        offset = offset * chunk_size_ + base_;
        new ((SlabHeader *)offset) SlabHeader(this);

        return (void *)(offset + SLAB_HEADER_SIZE);
    }

    void Slab::free_obj(const void *__p) noexcept {
        uint64_t offset = (uint64_t)__p;
        offset = (offset - base_) / chunk_size_;
        reset(offset);

        belong_cache_->free(this);
    }

    // slab header
    SlabHeader::SlabHeader(Slab *__slab) : slab(__slab), magic(SLAB_HEADER_MAGIC) {}

    // cache
    Cache::Cache(uint32_t __flags, uint32_t __obj_size, uint32_t __buddy_order, uint32_t __nr_static_slab_nodes, ...)
        : flags_(__flags), obj_size_(__obj_size), buddy_order_(__buddy_order), nr_objs_(0), nr_free_objs(0) {
        chunk_size_ = LONG_ALIGN((__obj_size + SLAB_HEADER_SIZE));
        objs_per_slab_ = get_pblk_size(__buddy_order) / chunk_size_;

        va_list static_slabs;
        va_start(static_slabs, __nr_static_slab_nodes);

        while (__nr_static_slab_nodes--) {
            auto node = va_arg(static_slabs, ktl::intrusive_list_node<Slab *> *);
            Slab *slab = **node;
            avl_list.insert(node);

            slab->belong_cache_ = this;
            nr_objs_ += slab->nr_objs();
            nr_free_objs += slab->nr_free_objs();
        }

        va_end(static_slabs);
    }

    void Cache::free(Slab *__slab) noexcept {
        ++nr_free_objs;

        if (__slab->flags_ & SLAB_FULL) {
            auto iter = full_list.begin();
            while (iter != full_list.end()) {
                if (*iter == __slab)
                    break;
                ++iter;
            }

            if (iter != full_list.end()) {
                full_list.erase(iter);
                avl_list.insert(iter._node);

                __slab->flags_ &= ~SLAB_FULL;
            }
        }
    }

    void *Cache::alloc(gfm_t __gfm) noexcept {
        if (avl_list.empty()) {
            ptr_t heap = alloc_pages(buddy_order_, __gfm);
            ptr8_t bp_heap = (ptr8_t)kmalloc((objs_per_slab_ + 7) / 8, __gfm);
            if (heap == nullptr || bp_heap == nullptr)
                return nullptr;

            Slab *s = (Slab *)kmalloc(sizeof(Slab));
            if (s == nullptr)
                return nullptr;
            new (s) Slab(heap, 0, objs_per_slab_, chunk_size_, this, bp_heap);

            auto s_node = (ktl::intrusive_list_node<Slab *> *)kmalloc(sizeof(ktl::intrusive_list_node<Slab *>));
            if (s_node == nullptr)
                return nullptr;
            new (s_node) ktl::intrusive_list_node<Slab *>(s);

            avl_list.insert(s_node);
        }

        auto first = avl_list.begin();
        void *ret = (*first)->alloc_obj(__gfm);

        if ((*first)->nr_free_objs() == 0) {
            avl_list.erase(first);
            full_list.insert(first._node);

            (*first)->flags_ |= SLAB_FULL;
        }

        if (ret != nullptr)
            nr_free_objs--;

        return ret;
    }

    // cache chain
    void CacheChain::__add_cache(Cache *__cache) noexcept {
        // auto node = new ktl::intrusive_list_node<Cache *>(__cache);
        auto node =
            (ktl::intrusive_list_node<Cache *> *)kmalloc(sizeof(ktl::intrusive_list_node<Cache *>), GFM_PERFECT_FIT);
        if (node == nullptr)
            return;
        new (node) ktl::intrusive_list_node<Cache *>(__cache);

        __add_cache(node);
    }

    void CacheChain::__add_cache(ktl::intrusive_list_node<Cache *> *__cache_node) noexcept {
        auto iter = cache_list.begin();
        auto cache = **__cache_node;
        while (iter != cache_list.end()) {
            if ((*iter)->obj_size_ > cache->obj_size_)
                break;
            iter++;
        }

        cache_list.insert(iter, __cache_node);
    }

    void *CacheChain::alloc(uint32_t __size, gfm_t __gfm) noexcept {
        bool need_perfect_fit = __gfm & _GFM_PERFECT_FIT;

        Cache *cache = nullptr;
        for (auto c : cache_list) {
            if (c->obj_size_ == __size) {
                cache = c;
                break;
            } else if (c->obj_size_ > __size && !need_perfect_fit) {
                cache = c;
                break;
            }
        }

        if (!cache) {
            if (need_perfect_fit && __gfm & _GFM_NEW_CACHE_TO_PFIT) {
                // TODO new cache to fit
                return nullptr;
            }
        }

        if (cache)
            return cache->alloc(__gfm);

        return nullptr;
    }

}   // namespace mm
NAMESPACE_BIGOS_END
