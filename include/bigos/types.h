#ifndef _BIG_TYPES_H
#define _BIG_TYPES_H

#include <stdint.h>
#include <stddef.h>

// get free memory
typedef unsigned int gfm_t;

typedef void* ptr_t;
typedef uint8_t* ptr8_t;
typedef uint16_t* ptr16_t;
typedef uint32_t* ptr32_t;
typedef uint64_t* ptr64_t;

#define NAMESPACE_BIGOS_BEG namespace bigos {
#define NAMESPACE_BIGOS_END }

#define NAMESPACE_DRIVER_BEG namespace driver {
#define NAMESPACE_DRIVER_END }

#endif   // _BIG_TYPES_H
