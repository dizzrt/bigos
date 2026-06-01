#ifndef _BIG_KMEM_H
#define _BIG_KMEM_H

#include <bigos/types.h>
#include <bigos/attributes.h>
#include <arch/x86/boot/boot_info.h>

NAMESPACE_BIGOS_BEG
namespace mm {
    void init_mem(const BootInfoHeader *__boot_info) noexcept;
    namespace __detail {
        void *kmem_cache_alloc(size_t size, gfm_t flags) _attr_malloc_;
        void *kmem_memory_alloc_pages(uint32_t nr_pages, gfm_t gfm) _attr_malloc_;
    }   // namespace __detail
}   // namespace mm
NAMESPACE_BIGOS_END
#endif   // _BIG_KMEM_H
