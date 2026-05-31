#include <bigos/memory.h>
#include <bigos/types.h>

// operator new
void *operator new(size_t size) {
    return bigos::kmalloc(size);
}

void *operator new[](size_t size) {
    return bigos::kmalloc(size);
}

// operator delete
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
