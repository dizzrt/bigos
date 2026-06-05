#ifndef _BIG_MEMORY_H
#define _BIG_MEMORY_H

#include <bigos/types.h>
#include <bigos/attributes.h>
#include <arch/x86/boot/boot_info.h>

NAMESPACE_BIGOS_BEG

// Allocates kernel virtual pages by page count, not by buddy order.
_attr_nodiscard_ extern void *alloc_kernel_pages(uint32_t __pages, gfm_t __gfm) noexcept _attr_malloc_;

// Releases a kernel virtual range returned by alloc_kernel_pages().
extern void free_pages(const void *__p) noexcept;

_attr_nodiscard_ extern void *kmalloc(size_t __size, gfm_t __gfm = 0) noexcept _attr_malloc_;

extern void free(const void *__p) noexcept;

void init_mem(const BootInfoHeader *__boot_info) noexcept;

namespace mm {
    constexpr uintptr_t KDIRECT_BASE = 0xffff900000000000ul;
    constexpr uintptr_t KDIRECT_LEN = 0x400000000000ul;
    constexpr uint64_t INVALID_PHYS_ADDR = ~0ull;

    struct SlabAllocatorStats;

    bool is_direct_mapped_phys(uint64_t __phys, uint64_t __len = 1) noexcept;
    _attr_nodiscard_ void *phys_to_direct(uint64_t __phys) noexcept;
    uint64_t direct_to_phys(const void *__addr) noexcept;

    extern void collect_slab_stats(SlabAllocatorStats *__stats) noexcept;
    extern void print_slab_stats() noexcept;

    void self_test() noexcept;
}   // namespace mm

NAMESPACE_BIGOS_END

#endif   // _BIG_MEMORY_H
