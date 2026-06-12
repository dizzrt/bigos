#ifndef _BIG_KMEM_H
#define _BIG_KMEM_H

#include <bigos/types.h>
#include <bigos/attributes.h>
#include <arch/x86/boot/boot_info.h>

NAMESPACE_BIGOS_BEG
namespace mm {
    void init_mem(const BootInfoHeader *__boot_info) noexcept;
}   // namespace mm
NAMESPACE_BIGOS_END
#endif   // _BIG_KMEM_H
