#include <string.h>

void *memset(void *_s, int _c, size_t n) noexcept {
    char *s = (char *)_s;
    for (size_t i = 0; i < n; i++)
        s[i] = _c;

    return _s;
}

void *memcpy(void *_dest, const void *_src, size_t _n) noexcept {
    char *dest_ptr = (char *)_dest;
    const char *src_ptr = (const char *)_src;

    if (_n) {
        _n++;
        while (--_n > 0)
            *dest_ptr++ = *src_ptr++;
    }

    return _dest;
}

size_t strlen(const char *_s) noexcept {
    const char *end = _s;

    while (*end++)
        ;

    return (--end) - _s;
}
