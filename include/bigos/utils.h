#ifndef _BIG_UTILS_H
#define _BIG_UTILS_H

#include "types.h"

namespace bigos {
    char *itoa(int64_t _value, char *_buffer, uint8_t _radix);
    char *utoa(uint64_t _value, char *_buffer, uint8_t _radix);

}   // namespace bigos

#endif