#ifndef _BIG_TIME_H
#define _BIG_TIME_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace time {
    // Fixed degradation baseline used when the RTC read fails or yields an
    // out-of-range field. boot_unix_time() becomes this value and the wall clock
    // still advances monotonically from it via the kernel tick.
    constexpr int64_t RTC_INVALID_BASELINE = 0;

    // Deterministic marker emitted to COM1/VGA when the RTC baseline read fails
    // and the wall clock degrades to RTC_INVALID_BASELINE.
    constexpr const char *RTC_INVALID_MARKER = "BIGOS_RTC_INVALID\n";

    // Converts a civil (proleptic Gregorian) date to a day count relative to
    // 1970-01-01 (Unix epoch). Pure arithmetic, no IO/alloc; valid for the small
    // 2000.. range this kernel produces. Based on Howard Hinnant's days_from_civil.
    int64_t days_from_civil(int64_t __year, int64_t __month, int64_t __day) noexcept;

    // Non-interrupt context only. Reads the CMOS RTC once (after the monotonic
    // tick is available), converts the UTC civil time to Unix epoch seconds as the
    // wall-clock baseline, and records the current tick. On RTC failure it sets
    // the baseline to RTC_INVALID_BASELINE and emits RTC_INVALID_MARKER. It never
    // panics, blocks, or allocates. Must be called once, before proc::init().
    void init() noexcept;

    // Returns the boot wall-clock baseline (Unix epoch seconds) established by
    // init(). Read-only, no hardware access, no allocation, no blocking.
    int64_t boot_unix_time() noexcept;

    // Returns the current wall-clock time in Unix epoch seconds:
    //   boot_unix_time + (timer::ticks() - boot_tick) / TIMER_HZ.
    // Monotonic non-decreasing under the single-core tick model. Read-only, no
    // hardware access, no allocation, no blocking.
    int64_t current_unix_time() noexcept;
}   // namespace time
NAMESPACE_BIGOS_END

#endif   // _BIG_TIME_H
