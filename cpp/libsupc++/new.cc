#include <bigos/memory.h>
#include <bigos/types.h>

// Global new/delete are ordinary kmalloc/free frontends and are non-IRQ-handler-safe.
// They do not provide emergency or interrupt-context allocation semantics.
void *operator new(size_t size) {
    return bigos::kmalloc(size);
}

void *operator new[](size_t size) {
    return bigos::kmalloc(size);
}

void operator delete(void *p) {
    bigos::free(p);
}

void operator delete[](void *p) {
    bigos::free(p);
}

void operator delete(void *p, size_t size) {
    bigos::free(p);
}

void operator delete[](void *p, size_t size) {
    bigos::free(p);
}
