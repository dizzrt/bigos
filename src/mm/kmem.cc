#include <type_traits>
#include <bigos/io.h>   // TODO remove later

#include "kmem.h"
#include "vmem.h"
#include "buddy.h"
#include "slab.h"

namespace bigos::mm::__detail {
    struct slab_order_compare {
        constexpr const size_t &operator()(const size_t &x, const size_t &y) const {
            if ((0x00000000ffffffff & x) <= (0x00000000ffffffff & y))
                return x;
            else
                return y;
        }
    };
}   // namespace bigos::mm::__detail

// pblk => page block
#define get_pblk_rest(CHUNK_SIZE, PBLK_SIZE) (PBLK_SIZE - PBLK_SIZE / CHUNK_SIZE * CHUNK_SIZE)
#define pblk_calc_diff(CHUNK_SIZE, ORDER)    (ORDER << 32) | (get_pblk_rest(CHUNK_SIZE, get_pblk_size(ORDER)))
#define pblk_all_diff(CHUNK_SIZE)                                                                                      \
    (pblk_calc_diff(CHUNK_SIZE, 0ul)), (pblk_calc_diff(CHUNK_SIZE, 1ul)), (pblk_calc_diff(CHUNK_SIZE, 2ul)),           \
        (pblk_calc_diff(CHUNK_SIZE, 3ul)), (pblk_calc_diff(CHUNK_SIZE, 4ul)), (pblk_calc_diff(CHUNK_SIZE, 5ul)),       \
        (pblk_calc_diff(CHUNK_SIZE, 6ul)), (pblk_calc_diff(CHUNK_SIZE, 7ul)), (pblk_calc_diff(CHUNK_SIZE, 8ul)),       \
        (pblk_calc_diff(CHUNK_SIZE, 9ul)), (pblk_calc_diff(CHUNK_SIZE, 10ul))
#define pblk_diff_to_order(DIFF) (DIFF >> 32)
#define pblk_min_diff(CHUNK_SIZE)                                                                                      \
    bigos::ulong_list_compare<bigos::mm::__detail::slab_order_compare, pblk_all_diff(CHUNK_SIZE)>::value
#define pblk_perfect_order(CHUNK_SIZE) pblk_diff_to_order(pblk_min_diff(CHUNK_SIZE))

#define slab_chunk_size(OBJ_SIZE)   LONG_ALIGN((OBJ_SIZE + SLAB_HEADER_SIZE))
#define slab_heap_size(OBJ_SIZE)    get_pblk_size(pblk_perfect_order(slab_chunk_size(OBJ_SIZE)))
#define slab_nr_objs(OBJ_SIZE)      (slab_heap_size(OBJ_SIZE) / slab_chunk_size(OBJ_SIZE))
#define slab_bp_heap_size(OBJ_SIZE) ((slab_nr_objs(OBJ_SIZE) + 7) / 8)

#define static_slab(NAME, OBJ_SIZE, FLAGS)                                                                             \
    static uint8_t _section_4k_align_ __static_slab_heap_##NAME[slab_heap_size(OBJ_SIZE)];                             \
    static uint8_t __static_slab_bp_heap_##NAME[slab_bp_heap_size(OBJ_SIZE)];                                          \
    static bigos::mm::Slab __static_slab_##NAME((ptr_t)__static_slab_heap_##NAME, FLAGS, slab_nr_objs(OBJ_SIZE),       \
        slab_chunk_size(OBJ_SIZE), nullptr, __static_slab_bp_heap_##NAME);                                             \
    static ktl::intrusive_list_node<bigos::mm::Slab *> static_slab_node_##NAME(&__static_slab_##NAME);

#define static_cache(NAME, OBJ_SIZE, FLAGS, NR_STATIC_SLABS, ...)                                                      \
    static bigos::mm::Cache __static_cache_##NAME(                                                                     \
        FLAGS, OBJ_SIZE, pblk_perfect_order(OBJ_SIZE), NR_STATIC_SLABS, ##__VA_ARGS__);                                \
    static ktl::intrusive_list_node<bigos::mm::Cache *> static_cache_node_##NAME(&__static_cache_##NAME);

// static slabs
static_slab(1B, sizeof(uint8_t), SLAB_PERMANENT);
static_slab(2B, sizeof(uint16_t), SLAB_PERMANENT);
static_slab(4B, sizeof(uint32_t), SLAB_PERMANENT);
static_slab(8B, sizeof(uint64_t), SLAB_PERMANENT);
static_slab(16B, 16, SLAB_PERMANENT);
static_slab(32B, 32, SLAB_PERMANENT);
static_slab(64B, 64, SLAB_PERMANENT);
static_slab(128B, 128, SLAB_PERMANENT);
static_slab(256B, 256, SLAB_PERMANENT);
static_slab(512B, 512, SLAB_PERMANENT);
static_slab(1024B, 1024, SLAB_PERMANENT);
static_slab(2048B, 2048, SLAB_PERMANENT);

static_slab(cache, sizeof(bigos::mm::Cache), SLAB_PERMANENT);
static_slab(slab_1, sizeof(bigos::mm::Slab), SLAB_PERMANENT);
static_slab(slab_2, sizeof(bigos::mm::Slab), SLAB_PERMANENT);
static_slab(pblk_1, PAGE_BLOCK_SIZE, SLAB_PERMANENT);
static_slab(pblk_2, PAGE_BLOCK_SIZE, SLAB_PERMANENT);
static_slab(intrusive_list_node_ptr_1, sizeof(ktl::intrusive_list_node<ptr_t>), SLAB_PERMANENT);
static_slab(intrusive_list_node_ptr_2, sizeof(ktl::intrusive_list_node<ptr_t>), SLAB_PERMANENT);

// static caches
static_cache(1B, sizeof(uint8_t), 0, 1, &static_slab_node_1B);
static_cache(2B, sizeof(uint16_t), 0, 1, &static_slab_node_2B);
static_cache(4B, sizeof(uint32_t), 0, 1, &static_slab_node_4B);
static_cache(8B, sizeof(uint64_t), 0, 1, &static_slab_node_8B);
static_cache(16B, 16, 0, 1, &static_slab_node_16B);
static_cache(32B, 32, 0, 1, &static_slab_node_32B);
static_cache(64B, 64, 0, 1, &static_slab_node_64B);
static_cache(128B, 128, 0, 1, &static_slab_node_128B);
static_cache(256B, 256, 0, 1, &static_slab_node_256B);
static_cache(512B, 512, 0, 1, &static_slab_node_512B);
static_cache(1024B, 1024, 0, 1, &static_slab_node_1024B);
static_cache(2048B, 2048, 0, 1, &static_slab_node_2048B);

static_cache(cache, sizeof(bigos::mm::Cache), 0, 1, &static_slab_node_cache);
static_cache(slab, sizeof(bigos::mm::Slab), 0, 2, &static_slab_node_slab_1, &static_slab_node_slab_2);
static_cache(pblk, PAGE_BLOCK_SIZE, 0, 2, &static_slab_node_pblk_1, &static_slab_node_pblk_2);
static_cache(intrusive_list_node_ptr, sizeof(ktl::intrusive_list_node<ptr_t>), 0, 2, &static_slab_node_intrusive_list_node_ptr_1,
    &static_slab_node_intrusive_list_node_ptr_2);

static bigos::mm::CacheChain kmem_cache;

NAMESPACE_BIGOS_BEG
namespace mm {
    namespace __detail {
        static void init_cache() noexcept {
            kmem_cache.__add_cache(&static_cache_node_2048B);
            kmem_cache.__add_cache(&static_cache_node_1024B);
            kmem_cache.__add_cache(&static_cache_node_512B);
            kmem_cache.__add_cache(&static_cache_node_256B);
            kmem_cache.__add_cache(&static_cache_node_128B);
            kmem_cache.__add_cache(&static_cache_node_64B);
            kmem_cache.__add_cache(&static_cache_node_32B);
            kmem_cache.__add_cache(&static_cache_node_16B);
            kmem_cache.__add_cache(&static_cache_node_8B);
            kmem_cache.__add_cache(&static_cache_node_4B);
            kmem_cache.__add_cache(&static_cache_node_2B);
            kmem_cache.__add_cache(&static_cache_node_1B);

            kmem_cache.__add_cache(&static_cache_node_cache);
            kmem_cache.__add_cache(&static_cache_node_slab);
            kmem_cache.__add_cache(&static_cache_node_pblk);
            kmem_cache.__add_cache(&static_cache_node_intrusive_list_node_ptr);
        }
    }   // namespace __detail

    void init_mem(const BootInfoHeader *__boot_info) noexcept {
        __detail::init_cache();
        __detail::init_buddy(__boot_info);
        __detail::init_vmem();
    }
}   // namespace mm

// defined in memory.h
void *kmalloc(size_t __size, gfm_t __gfm) noexcept {
    return kmem_cache.alloc(__size, __gfm);
}

void free(const void *__p) noexcept {
    if (__p == nullptr)
        return;

    uint64_t addr = (uint64_t)__p;
    auto slab_header = (mm::SlabHeader *)(addr - SLAB_HEADER_SIZE);

    // check if a slab's object
    if (slab_header->magic != SLAB_HEADER_MAGIC)
        return;

    slab_header->slab->free_obj(__p);
}
NAMESPACE_BIGOS_END
