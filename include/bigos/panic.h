#ifndef _BIG_PANIC_H
#define _BIG_PANIC_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG

// Stable, source-segmented fatal diagnostic codes. Values are stable and
// ASCII-printable as hex so emulator serial smoke can assert on them.
// boot-early segment (0xf000) is reserved per design D7 and intentionally not wired.
enum class PanicCode : uint32_t {
    Generic = 0x0000,

    // mm-buddy
    BuddyMemoryHandoffFailed = 0x1000,

    // mm-arena
    EarlyMetadataArenaExhausted = 0x1100,

    // mm-slab
    SlabDebugGuard = 0x1200,

    // mm-vmem (reserved, not wired)
    VmemReserved = 0x1300,

    // self-test
    SelfTestFailure = 0x2000,

    // irq-exception
    IrqException = 0x3000,

    // irq-pagefault
    IrqPageFault = 0x3100,
};

// Unified halt primitive: disable maskable interrupts, then halt forever.
// Used by paths that already emitted their own diagnostic marker.
[[noreturn]] void khalt() noexcept;

// Unified fatal entry: emit a fixed `BIGOS_PANIC code=<code> source=<source>`
// marker line (constant strings before any variadic formatting, dual COM1+VGA),
// then halt via khalt(). MUST NOT allocate or depend on hosted runtime services.
[[noreturn]] void kpanic(PanicCode __code, const char *__source) noexcept;
[[noreturn]] void kpanic(PanicCode __code, const char *__source, const char *__fmt, ...) noexcept;

// Optional snapshot variant: emit the panic marker, then dump read-only allocator
// stats before halting. MUST NOT allocate or trigger paths that can fail again.
[[noreturn]] void kpanic_with_mm_stats(PanicCode __code, const char *__source) noexcept;

NAMESPACE_BIGOS_END

#endif   // _BIG_PANIC_H
