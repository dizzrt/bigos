#ifndef _BIG_TYPES_H
#define _BIG_TYPES_H

#include <stdint.h>
#include <stddef.h>

// LP64 x86_64 ABI guardrails: fail the build deterministically on a toolchain
// or data model that violates the assumptions BigOS depends on, instead of
// silently producing inconsistent binaries. The kernel and C++ support builds
// are LP64 (x86_64-elf); the UEFI loader shim that also includes this header is
// compiled with the LLP64 Windows x64 ABI, so the `long` width check is gated
// on the LP64 data model while the width-only checks hold on any x86_64 ABI.
static_assert(sizeof(size_t) == 8, "BigOS assumes x86_64: size_t is 8 bytes");
static_assert(sizeof(uint64_t) == 8 && sizeof(uint32_t) == 4, "fixed-width integer widths must match their nominal sizes");
static_assert(__CHAR_BIT__ == 8, "BigOS assumes an 8-bit byte");
#if defined(__LP64__) || defined(_LP64)
static_assert(sizeof(long) == 8, "BigOS assumes LP64 x86_64: long is 8 bytes");
#endif

// get free memory
typedef unsigned int gfm_t;

typedef void *ptr_t;
typedef uint8_t *ptr8_t;
typedef uint16_t *ptr16_t;
typedef uint32_t *ptr32_t;
typedef uint64_t *ptr64_t;

#define NAMESPACE_BIGOS_BEG namespace bigos {
#define NAMESPACE_BIGOS_END }

#define NAMESPACE_DRIVER_BEG namespace driver {
#define NAMESPACE_DRIVER_END }

#endif   // _BIG_TYPES_H
