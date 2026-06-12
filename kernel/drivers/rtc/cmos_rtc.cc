#include <drivers/rtc/cmos_rtc.h>

#include <bigos/io.h>

NAMESPACE_DRIVER_BEG
namespace rtc {
    namespace {
        // Select a CMOS register on port 0x70, then read its value from 0x71.
        uint8_t read_register(uint8_t __index) noexcept {
            bigos::outb(CMOS_INDEX_PORT, __index);
            return bigos::inb(CMOS_DATA_PORT);
        }

        // Bounded poll on the update-in-progress bit. Returns false if the bound
        // is exceeded so the caller degrades deterministically instead of
        // spinning forever.
        bool wait_update_not_in_progress() noexcept {
            for (uint32_t i = 0; i < UIP_POLL_LIMIT; i++) {
                if ((read_register(REG_STATUS_A) & STATUS_A_UIP) == 0)
                    return true;
            }
            return false;
        }

        // BCD -> binary: low nibble is the ones digit, high nibble the tens.
        uint8_t bcd_to_binary(uint8_t __value) noexcept {
            return (uint8_t)((__value & 0x0f) + ((__value >> 4) * 10));
        }

        bool in_range(int64_t __value, int64_t __low, int64_t __high) noexcept {
            return __value >= __low && __value <= __high;
        }
    }   // namespace

    bool read_time(DateTime *__out) noexcept {
        if (__out == nullptr)
            return false;

        if (!wait_update_not_in_progress())
            return false;

        const uint8_t status_b = read_register(REG_STATUS_B);
        const bool binary_mode = (status_b & STATUS_B_BINARY) != 0;
        const bool hour_24 = (status_b & STATUS_B_24_HOUR) != 0;

        uint8_t second = read_register(REG_SECONDS);
        uint8_t minute = read_register(REG_MINUTES);
        uint8_t hour_raw = read_register(REG_HOURS);
        uint8_t day = read_register(REG_DAY_OF_MONTH);
        uint8_t month = read_register(REG_MONTH);
        uint8_t year = read_register(REG_YEAR);

        // In 12-hour mode the PM flag lives in bit 7 of the (still encoded) hour
        // register; strip it before BCD conversion and re-apply afterwards.
        const bool hour_pm = !hour_24 && (hour_raw & HOUR_PM_FLAG) != 0;
        uint8_t hour = (uint8_t)(hour_raw & (uint8_t)~HOUR_PM_FLAG);

        if (!binary_mode) {
            second = bcd_to_binary(second);
            minute = bcd_to_binary(minute);
            hour = bcd_to_binary(hour);
            day = bcd_to_binary(day);
            month = bcd_to_binary(month);
            year = bcd_to_binary(year);
        }

        int64_t hour24 = (int64_t)hour;
        if (!hour_24) {
            // 12-hour clock: 12 AM -> 0, 12 PM -> 12, otherwise add 12 for PM.
            if (hour24 == 12)
                hour24 = 0;
            if (hour_pm)
                hour24 += 12;
        }

        DateTime result;
        result.year = RTC_CENTURY_BASE + (int64_t)year;
        result.month = (int64_t)month;
        result.day = (int64_t)day;
        result.hour = hour24;
        result.minute = (int64_t)minute;
        result.second = (int64_t)second;

        if (!in_range(result.month, 1, 12) || !in_range(result.day, 1, 31) || !in_range(result.hour, 0, 23) ||
            !in_range(result.minute, 0, 59) || !in_range(result.second, 0, 59) ||
            !in_range((int64_t)year, 0, 99))
            return false;

        *__out = result;
        return true;
    }
}   // namespace rtc
NAMESPACE_DRIVER_END
