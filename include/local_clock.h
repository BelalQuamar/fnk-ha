#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>

constexpr const char* ntpPrimary = "pool.ntp.org";
constexpr const char* ntpSecondary = "time.google.com";
constexpr time_t firstSynchronizedTime = 1700000000;

struct ClockTransition {
    int32_t epoch;
    int32_t offset;
    const char* abbreviation;
};

struct ClockZone {
    const char* name;
    uint16_t first;
    uint16_t count;
};

#include "clock_timezones.inc"

inline const ClockZone* findClockZone(const char* name) {
    size_t lower = 0, upper = sizeof(clockZones) / sizeof(clockZones[0]);
    while (lower < upper) {
        const size_t middle = (lower + upper) / 2;
        const int result = std::strcmp(name, clockZones[middle].name);
        if (result == 0) return &clockZones[middle];
        if (result < 0) upper = middle;
        else lower = middle + 1;
    }
    return nullptr;
}

// Apply the IANA offset to UTC, then use gmtime for date and clock fields.
inline const char* clockLocalTime(time_t utc, const ClockZone& zone, tm& local) {
    auto transition = clockTransitions + zone.first;
    const auto end = transition + zone.count;
    while (transition + 1 < end && utc >= (transition + 1)->epoch) ++transition;
    const int64_t adjusted = static_cast<int64_t>(utc) + transition->offset;
    // Keep the time_t conversion defined near the 32-bit clock's upper bound.
    const time_t time = adjusted > INT32_MAX ? INT32_MAX : adjusted;
    gmtime_r(&time, &local);
    return transition->abbreviation;
}
