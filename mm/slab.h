#ifndef _BIG_SLAB_H
#define _BIG_SLAB_H

#include <ktl/list.h>
#include <ktl/bitset.h>
#include <bigos/types.h>

#define LONG_ALIGN(SIZE) ((SIZE + sizeof(long) - 1) & ~(sizeof(long) - 1))

#define SLAB_HEADER_SIZE  sizeof(bigos::mm::SlabHeader)
#define SLAB_HEADER_MAGIC 0x50b7ff2785ff7b22

#define SLAB_FULL      (1u << 0)
#define SLAB_PERMANENT (1u << 1)

NAMESPACE_BIGOS_BEG
namespace mm {
    class Cache;

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
    };

    struct SlabHeader {
        Slab *slab;
        const uint64_t magic;

        SlabHeader(Slab *__slab);
    };

    class Cache {
    private:
        friend class CacheChain;

        ktl::intrusive_list<Slab *> avl_list;
        ktl::intrusive_list<Slab *> full_list;

        uint32_t flags_;
        uint32_t obj_size_;
        uint32_t chunk_size_;
        uint32_t buddy_order_;
        uint32_t objs_per_slab_;
        uint32_t nr_objs_;
        uint32_t nr_free_objs;
        uint32_t alignment__;

    public:
        // constructors
        Cache(uint32_t __flags, uint32_t __obj_size, uint32_t __buddy_order, uint32_t __nr_static_slab_nodes, ...);

        // destructors
        ~Cache() = default;

        void free(Slab *__slab) noexcept;
        void *alloc(gfm_t __gfm) noexcept _attr_malloc_;
    };

    class CacheChain {
    private:
        ktl::intrusive_list<Cache *> cache_list;

    public:
        void __add_cache(Cache *__cache) noexcept;
        void __add_cache(ktl::intrusive_list_node<Cache *> *__cache_node) noexcept;

        void *alloc(uint32_t __size, gfm_t __gfm) noexcept _attr_malloc_;
    };
}   // namespace mm
NAMESPACE_BIGOS_END
#endif   // _BIG_SLAB_H
