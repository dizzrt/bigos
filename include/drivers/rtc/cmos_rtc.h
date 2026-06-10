#ifndef _BIG_CMOS_RTC_H
#define _BIG_CMOS_RTC_H

#include <bigos/types.h>

NAMESPACE_DRIVER_BEG
namespace rtc {
    // CMOS RTC index/data ports (legacy ISA). 0x70 selects the register, 0x71
    // reads/writes the selected register's value.
    constexpr uint16_t CMOS_INDEX_PORT = 0x70;
    constexpr uint16_t CMOS_DATA_PORT = 0x71;

    // CMOS register indices used by the one-shot wall-clock baseline read.
    constexpr uint8_t REG_SECONDS = 0x00;
    constexpr uint8_t REG_MINUTES = 0x02;
    constexpr uint8_t REG_HOURS = 0x04;
    constexpr uint8_t REG_DAY_OF_MONTH = 0x07;
    constexpr uint8_t REG_MONTH = 0x08;
    constexpr uint8_t REG_YEAR = 0x09;
    constexpr uint8_t REG_STATUS_A = 0x0a;
    constexpr uint8_t REG_STATUS_B = 0x0b;

    // Status register A bit 7: update-in-progress. Reading time fields while it
    // is set risks a half-updated value, so the driver polls until it clears
    // (bounded by UIP_POLL_LIMIT).
    constexpr uint8_t STATUS_A_UIP = 1u << 7;
    // Status register B bit 1: 24-hour mode when set, 12-hour when clear.
    constexpr uint8_t STATUS_B_24_HOUR = 1u << 1;
    // Status register B bit 2: binary mode when set, BCD when clear.
    constexpr uint8_t STATUS_B_BINARY = 1u << 2;
    // 12-hour mode hour register bit 7: PM flag.
    constexpr uint8_t HOUR_PM_FLAG = 1u << 7;

    // Bounded UIP poll count. The query path never reaches here; only the single
    // baseline read does, so the cap stays small and deterministic.
    constexpr uint32_t UIP_POLL_LIMIT = 1000000;

    // Century is fixed at 2000 (no ACPI FADT century register); the RTC year
    // field 0..99 maps to 2000..2099.
    constexpr int64_t RTC_CENTURY_BASE = 2000;

    // Normalized UTC civil date/time read once at boot. Fields are already
    // converted out of BCD and 12/24-hour encodings into plain values.
    struct DateTime {
        int64_t year;     // full year, e.g. 2025
        int64_t month;    // 1..12
        int64_t day;      // 1..31
        int64_t hour;     // 0..23
        int64_t minute;   // 0..59
        int64_t second;   // 0..59
    };

    // One-shot read of the CMOS RTC over ports 0x70/0x71. Polls the UIP bit
    // (bounded), reads status register B to decode BCD and 12/24-hour mode,
    // normalizes the fields, and validates each field's range. Returns true and
    // fills *__out on success; returns false (leaving *__out untouched) when the
    // UIP poll exceeds its bound or any field is out of range. This routine is
    // non-IRQ-context only: it does no IRQ8 registration and no periodic polling,
    // and it must not be called from the wall-clock query hot path.
    bool read_time(DateTime *__out) noexcept;
}   // namespace rtc
NAMESPACE_DRIVER_END

#endif   // _BIG_CMOS_RTC_H
