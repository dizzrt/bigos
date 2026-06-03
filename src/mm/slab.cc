#include <new>
#include <stdarg.h>
#include <string.h>
#include <bigos/io.h>   // TODO remove later

#include "slab.h"
#include "buddy.h"
#include <bigos/memory.h>
#include "memdef.h"

namespace {
    constexpr uint8_t SLAB_POISON_FREE = 0x5a;

    void slab_debug_fail(const char *__reason) noexcept {
#ifdef BIGOS_SLAB_DEBUG
        bigos::kprintf("slab debug guard: %s\n", __reason);
        while (true) {
            asm volatile("hlt");
        }
#else
        (void)__reason;
#endif
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace mm {
    static uint32_t g_large_allocation_count;
    static uint32_t g_large_allocation_pages;
    static uint32_t g_large_allocation_bytes;
    static uint32_t g_large_allocation_peak_count;
    static uint32_t g_large_allocation_peak_pages;
#ifdef BIGOS_SLAB_DEBUG
    static const void *g_recent_freed_large_payloads[16];
    static uint32_t g_recent_freed_large_index;
#endif

    static void account_large_alloc(uint32_t __pages, uint32_t __bytes) noexcept {
        g_large_allocation_count++;
        g_large_allocation_pages += __pages;
        g_large_allocation_bytes += __bytes;

        if (g_large_allocation_count > g_large_allocation_peak_count)
            g_large_allocation_peak_count = g_large_allocation_count;
        if (g_large_allocation_pages > g_large_allocation_peak_pages)
            g_large_allocation_peak_pages = g_large_allocation_pages;
    }

    static void account_large_free(uint32_t __pages, uint32_t __bytes) noexcept {
        if (g_large_allocation_count == 0 || g_large_allocation_pages < __pages || g_large_allocation_bytes < __bytes) {
            slab_debug_fail("large allocation accounting underflow");
            return;
        }

        g_large_allocation_count--;
        g_large_allocation_pages -= __pages;
        g_large_allocation_bytes -= __bytes;
    }

    static void remember_recent_large_free(const void *__p) noexcept {
#ifdef BIGOS_SLAB_DEBUG
        g_recent_freed_large_payloads[g_recent_freed_large_index % 16] = __p;
        g_recent_freed_large_index++;
#else
        (void)__p;
#endif
    }

    bool was_recent_large_free(const void *__p) noexcept {
#ifdef BIGOS_SLAB_DEBUG
        for (uint32_t i = 0; i < 16; i++) {
            if (g_recent_freed_large_payloads[i] == __p)
                return true;
        }
#else
        (void)__p;
#endif
        return false;
    }

    void *alloc_large(uint32_t __size, gfm_t __gfm) noexcept {
        if (__size <= CACHE_MAX_OBJ_SIZE)
            return nullptr;

        uint32_t bytes = (uint32_t)LONG_ALIGN(__size + SLAB_HEADER_SIZE);
        uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        ptr_t base = alloc_kernel_pages(pages, __gfm | _GFM_PRE_PAGING);
        if (base == nullptr)
            return nullptr;

        auto header = new ((SlabHeader *)base) SlabHeader(AllocationKind::LargePages, pages, __size, base);
        account_large_alloc(pages, __size);

        return (void *)((uint64_t)header + SLAB_HEADER_SIZE);
    }

    bool free_large(SlabHeader *__header, const void *__p) noexcept {
        if (__header == nullptr || __header->magic != SLAB_LARGE_ALLOC_MAGIC ||
            __header->kind != AllocationKind::LargePages)
            return false;

        ptr_t base = __header->base;
        uint32_t pages = __header->nr_pages;
        uint32_t requested_size = __header->requested_size;

        if (__p != (const void *)((uint64_t)base + SLAB_HEADER_SIZE)) {
            slab_debug_fail("large allocation invalid boundary");
            return false;
        }

#ifdef BIGOS_SLAB_DEBUG
        __header->magic = 0;
        if (requested_size != 0)
            memset((void *)__p, SLAB_POISON_FREE, requested_size);
#endif

        account_large_free(pages, requested_size);
        remember_recent_large_free(__p);
        free_pages(base);
        return true;
    }

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
        if (__p == nullptr || (uint64_t)__p < base_ + SLAB_HEADER_SIZE)
            return;

        uint64_t offset = (uint64_t)__p;
        if (offset >= base_ + (uint64_t)nr_objs() * chunk_size_)
            return;

        uint64_t chunk_offset = offset - base_;
        if (chunk_offset % chunk_size_ != SLAB_HEADER_SIZE) {
            slab_debug_fail("slab object invalid boundary");
            return;
        }

        offset = chunk_offset / chunk_size_;
        if (!test((uint32_t)offset)) {
            slab_debug_fail("slab object double free");
            return;
        }

#ifdef BIGOS_SLAB_DEBUG
        memset((void *)__p, SLAB_POISON_FREE, belong_cache_->obj_size_);
#endif

        if (reset((uint32_t)offset) != 1)
            return;

        belong_cache_->free(this);
    }

    // slab header
    SlabHeader::SlabHeader(Slab *__slab)
        : magic(SLAB_HEADER_MAGIC),
          kind(AllocationKind::SlabObject),
          flags(0),
          slab(__slab),
          nr_pages(0),
          requested_size(0),
          base(nullptr) {}

    SlabHeader::SlabHeader(AllocationKind __kind, uint32_t __nr_pages, uint32_t __requested_size, ptr_t __base)
        : magic(SLAB_LARGE_ALLOC_MAGIC),
          kind(__kind),
          flags(0),
          slab(nullptr),
          nr_pages(__nr_pages),
          requested_size(__requested_size),
          base(__base) {}

    // cache
    Cache::Cache(uint32_t __flags, uint32_t __obj_size, uint32_t __buddy_order, uint32_t __nr_static_slab_nodes, ...)
        : flags_(__flags),
          obj_size_(__obj_size),
          buddy_order_(__buddy_order),
          nr_objs_(0),
          nr_free_objs(0),
          nr_reclaimed_slabs_(0) {
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

    bool Cache::should_reclaim_empty_slab(Slab *__slab) const noexcept {
        if (__slab == nullptr || (__slab->flags_ & SLAB_PERMANENT) || __slab->nr_used_objs() != 0)
            return false;

        // Keep at least one available slab to avoid immediate grow/reclaim churn.
        return avl_list.size() > 1;
    }

    void Cache::reclaim_empty_slab(Slab *__slab) noexcept {
        auto iter = avl_list.begin();
        while (iter != avl_list.end()) {
            if (*iter == __slab)
                break;
            ++iter;
        }

        if (iter == avl_list.end())
            return;

        auto node = iter._node;
        avl_list.erase(iter);

        ptr_t heap = (ptr_t)__slab->base();
        ptr8_t bitmap = __slab->bitmap_heap();
        nr_objs_ -= __slab->nr_objs();
        nr_free_objs -= __slab->nr_free_objs();
        nr_reclaimed_slabs_++;

        bigos::free(bitmap);
        __slab->~Slab();
        bigos::free(__slab);
        bigos::free(node);
        free_pages(heap);
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

        if (should_reclaim_empty_slab(__slab))
            reclaim_empty_slab(__slab);
    }

    void *Cache::alloc(gfm_t __gfm) noexcept {
        if (avl_list.empty()) {
            // Slab growth must request virtual page counts and receive mapped backing.
            ptr_t heap = alloc_kernel_pages(1u << buddy_order_, __gfm | _GFM_PRE_PAGING);
            if (heap == nullptr)
                return nullptr;

            ptr8_t bp_heap = (ptr8_t)kmalloc((objs_per_slab_ + 7) / 8, __gfm);
            if (bp_heap == nullptr) {
                free_pages(heap);
                return nullptr;
            }

            Slab *s = (Slab *)kmalloc(sizeof(Slab));
            if (s == nullptr) {
                bigos::free(bp_heap);
                free_pages(heap);
                return nullptr;
            }
            new (s) Slab(heap, 0, objs_per_slab_, chunk_size_, this, bp_heap);

            auto s_node = (ktl::intrusive_list_node<Slab *> *)kmalloc(sizeof(ktl::intrusive_list_node<Slab *>));
            if (s_node == nullptr) {
                bigos::free(s);
                bigos::free(bp_heap);
                free_pages(heap);
                return nullptr;
            }
            new (s_node) ktl::intrusive_list_node<Slab *>(s);

            avl_list.insert(s_node);
            nr_objs_ += s->nr_objs();
            nr_free_objs += s->nr_free_objs();
        }

        auto first = avl_list.begin();
        void *ret = (*first)->alloc_obj(__gfm);
        if (ret == nullptr)
            return nullptr;

        if ((*first)->nr_free_objs() == 0) {
            avl_list.erase(first);
            full_list.insert(first._node);

            (*first)->flags_ |= SLAB_FULL;
        }

        if (ret != nullptr)
            nr_free_objs--;

        return ret;
    }

    void Cache::collect_stats(SlabCacheStats *__stats) const noexcept {
        if (__stats == nullptr)
            return;

        __stats->object_size = obj_size_;
        __stats->available_slab_count = (uint32_t)avl_list.size();
        __stats->full_slab_count = (uint32_t)full_list.size();
        __stats->slab_count = __stats->available_slab_count + __stats->full_slab_count;
        __stats->object_count = nr_objs_;
        __stats->free_object_count = nr_free_objs;
        __stats->used_object_count = nr_objs_ - nr_free_objs;
        __stats->reclaimed_slab_count = nr_reclaimed_slabs_;
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
                // Dynamic perfect-fit cache creation is intentionally unsupported.
                return nullptr;
            }
        }

        if (cache)
            return cache->alloc(__gfm);

        return nullptr;
    }

    void CacheChain::collect_stats(SlabAllocatorStats *__stats) const noexcept {
        if (__stats == nullptr)
            return;

        memset(__stats, 0, sizeof(*__stats));

        for (auto c : cache_list) {
            if (__stats->cache_count >= SlabAllocatorStats::MAX_CACHES)
                break;
            c->collect_stats(&__stats->caches[__stats->cache_count]);
            __stats->reclaimed_slab_count += __stats->caches[__stats->cache_count].reclaimed_slab_count;
            __stats->cache_count++;
        }

        __stats->large_allocation_count = g_large_allocation_count;
        __stats->large_allocation_pages = g_large_allocation_pages;
        __stats->large_allocation_bytes = g_large_allocation_bytes;
        __stats->large_allocation_peak_count = g_large_allocation_peak_count;
        __stats->large_allocation_peak_pages = g_large_allocation_peak_pages;
    }

}   // namespace mm
NAMESPACE_BIGOS_END
