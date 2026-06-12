#include <bigos/memory.h>

#include "kmem.h"
namespace bigos {
    void init_mem(const BootInfoHeader *__boot_info) noexcept {
        mm::init_mem(__boot_info);
    }
}   // namespace bigos
