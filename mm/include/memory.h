#ifndef _BIG_MEMORY_H
#define _BIG_MEMORY_H

#include <bigos/types.h>
#include <bigos/attributes.h>

NAMESPACE_BIGOS_BEG

_attr_nodiscard_ extern void *alloc_pages(uint32_t __pages, gfm_t __gfm) noexcept _attr_malloc_;

extern void free_pages(const void *__p) noexcept;

_attr_nodiscard_ extern void *kmalloc(size_t __size, gfm_t __gfm = 0) noexcept _attr_malloc_;

extern void free(const void *__p) noexcept;

void init_mem() noexcept;

NAMESPACE_BIGOS_END

#endif   // _BIG_MEMORY_H
