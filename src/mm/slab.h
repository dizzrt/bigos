#ifndef _BIG_SLAB_H
#define _BIG_SLAB_H

#include <ktl/list.h>
#include <ktl/bitset.h>
#include <bigos/types.h>

#define LONG_ALIGN(SIZE) ((SIZE + sizeof(long) - 1) & ~(sizeof(long) - 1))

#define SLAB_HEADER_SIZE       sizeof(bigos::mm::SlabHeader)
#define SLAB_HEADER_MAGIC      0x50b7ff2785ff7b22
#define SLAB_LARGE_ALLOC_MAGIC 0x6aa97e4110cfa11c

#define SLAB_FULL      (1u << 0)
#define SLAB_PERMANENT (1u << 1)

NAMESPACE_BIGOS_BEG
namespace mm {
    class Cache;

    enum class AllocationKind : uint32_t {
        SlabObject = 1,
        LargePages = 2,
    };

    class Slab : protected ktl::bitset {
    private:
        friend class Cache;

        uint64_t base_;
        uint32_t flags_;
        uint32_t chunk_size_;

        Cache *belong_cache_;

    public:
        // constructors
        Slab(ptr_t __heap, uint32_t __flags, uint32_t __nr_objs, uint32_t __chunk_size, Cache *__belong_cache,
            ptr8_t __bp_heap);

        // destructors
        ~Slab() = default;

        void *alloc_obj(gfm_t __gfm) noexcept _attr_malloc_;
        void free_obj(const void *__p) noexcept;

        inline const uint32_t nr_objs() const noexcept {
            return size();
        }
        inline const uint32_t nr_used_objs() const noexcept {
            return set_size();
        }
        inline const uint32_t nr_free_objs() const noexcept {
            return reset_size();
        }
        inline ptr8_t bitmap_heap() const noexcept {
            return heap_ptr_;
        }
        inline uint64_t base() const noexcept {
            return base_;
        }
    };

    struct SlabHeader {
        uint64_t magic;
        AllocationKind kind;
        uint32_t flags;
        Slab *slab;
        uint32_t nr_pages;
        uint32_t requested_size;
        ptr_t base;

        SlabHeader(Slab *__slab);
        SlabHeader(AllocationKind __kind, uint32_t __nr_pages, uint32_t __requested_size, ptr_t __base);
    };

    struct SlabCacheStats {
        uint32_t object_size;
        uint32_t slab_count;
        uint32_t available_slab_count;
        uint32_t full_slab_count;
        uint32_t object_count;
        uint32_t free_object_count;
        uint32_t used_object_count;
        uint32_t reclaimed_slab_count;
    };

    class Cache {
    private:
        friend class CacheChain;
        friend class Slab;

        ktl::intrusive_list<Slab *> avl_list;
        ktl::intrusive_list<Slab *> full_list;

        uint32_t flags_;
        uint32_t obj_size_;
        uint32_t chunk_size_;
        uint32_t buddy_order_;
        uint32_t objs_per_slab_;
        uint32_t nr_objs_;
        uint32_t nr_free_objs;
        uint32_t nr_reclaimed_slabs_;
        uint32_t alignment__;

        bool should_reclaim_empty_slab(Slab *__slab) const noexcept;
        void reclaim_empty_slab(Slab *__slab) noexcept;

    public:
        // constructors
        Cache(uint32_t __flags, uint32_t __obj_size, uint32_t __buddy_order, uint32_t __nr_static_slab_nodes, ...);

        // destructors
        ~Cache() = default;

        void free(Slab *__slab) noexcept;
        void *alloc(gfm_t __gfm) noexcept _attr_malloc_;
        void collect_stats(SlabCacheStats *__stats) const noexcept;
    };

    // Read-only allocator diagnostics. collect_slab_stats() snapshots this under
    // an IRQ-disabled-only boundary; print_slab_stats() is non-interrupt-context-only.
    struct SlabAllocatorStats {
        static constexpr uint32_t MAX_CACHES = 32;

        uint32_t cache_count;
        SlabCacheStats caches[MAX_CACHES];
        uint32_t large_allocation_count;
        uint32_t large_allocation_pages;
        uint32_t large_allocation_bytes;
        uint32_t large_allocation_peak_count;
        uint32_t large_allocation_peak_pages;
        uint32_t reclaimed_slab_count;
    };

    class CacheChain {
    private:
        ktl::intrusive_list<Cache *> cache_list;

    public:
        void __add_cache(Cache *__cache) noexcept;
        void __add_cache(ktl::intrusive_list_node<Cache *> *__cache_node) noexcept;

        void *alloc(uint32_t __size, gfm_t __gfm) noexcept _attr_malloc_;
        void collect_stats(SlabAllocatorStats *__stats) const noexcept;
    };

    // Internal slab/large-allocation helpers are non-IRQ-handler-safe.
    _attr_nodiscard_ void *alloc_large(uint32_t __size, gfm_t __gfm) noexcept _attr_malloc_;
    bool free_large(SlabHeader *__header, const void *__p) noexcept;
    bool was_recent_large_free(const void *__p) noexcept;
}   // namespace mm
NAMESPACE_BIGOS_END
#endif   // _BIG_SLAB_H
