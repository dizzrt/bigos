#include <bigos/time.h>

#include <bigos/device.h>
#include <bigos/io.h>
#include <bigos/timer.h>

NAMESPACE_BIGOS_BEG
namespace time {
    namespace {
        // Wall-clock baseline established once at init(): boot_unix_time seconds
        // and the monotonic tick captured at that instant. After init() these are
        // read-only; the query path only does arithmetic over timer::ticks().
        int64_t g_boot_unix_time = RTC_INVALID_BASELINE;
        bigos::timer::tick_t g_boot_tick = 0;
        bool g_initialized = false;

        constexpr int64_t SECONDS_PER_DAY = 86400;
        constexpr int64_t SECONDS_PER_HOUR = 3600;
        constexpr int64_t SECONDS_PER_MINUTE = 60;

        int64_t to_unix_seconds(const driver::rtc::DateTime &__dt) noexcept {
            const int64_t days = days_from_civil(__dt.year, __dt.month, __dt.day);
            return days * SECONDS_PER_DAY + __dt.hour * SECONDS_PER_HOUR + __dt.minute * SECONDS_PER_MINUTE +
                   __dt.second;
        }
    }   // namespace

    int64_t days_from_civil(int64_t __year, int64_t __month, int64_t __day) noexcept {
        // Shift the year so that March is the first month, putting the leap day
        // at the end of the era. Era = 400-year cycle.
        __year -= __month <= 2 ? 1 : 0;
        const int64_t era = (__year >= 0 ? __year : __year - 399) / 400;
        const int64_t yoe = __year - era * 400;                                   // [0, 399]
        const int64_t doy = (153 * (__month + (__month > 2 ? -3 : 9)) + 2) / 5 + __day - 1;   // [0, 365]
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                // [0, 146096]
        return era * 146097 + doe - 719468;
    }

    void init() noexcept {
        if (g_initialized)
            return;

        g_boot_tick = bigos::timer::ticks();

        driver::rtc::DateTime dt;
        if (bigos::device::read_rtc_time(&dt)) {
            g_boot_unix_time = to_unix_seconds(dt);
        } else {
            // Deterministic degradation: fixed baseline + marker, never panic /
            // block / allocate. The clock still advances from the captured tick.
            g_boot_unix_time = RTC_INVALID_BASELINE;
            bigos::serial_puts(RTC_INVALID_MARKER);
            bigos::kputs(RTC_INVALID_MARKER);
        }

        g_initialized = true;
    }

    int64_t boot_unix_time() noexcept {
        return g_boot_unix_time;
    }

    int64_t current_unix_time() noexcept {
        const bigos::timer::tick_t now = bigos::timer::ticks();
        const bigos::timer::tick_t elapsed = now - g_boot_tick;
        return g_boot_unix_time + (int64_t)(elapsed / bigos::timer::TIMER_HZ);
    }
}   // namespace time
NAMESPACE_BIGOS_END
