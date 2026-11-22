#include "local_clock.h"
#include <cassert>
#include <cstring>

void check(const char* zone, int month, int day, int utcHour, int utcMinute,
           int hour, int minute, const char* abbreviation) {
    tm utc{};
    utc.tm_year = 2026 - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = utcHour;
    utc.tm_min = utcMinute;
    const ClockZone* found = findClockZone(zone);
    assert(found);
    tm local{};
    const char* actual = clockLocalTime(timegm(&utc), *found, local);
    assert(local.tm_hour == hour && local.tm_min == minute);
    assert(std::strcmp(actual, abbreviation) == 0);
}

int main() {
    check("America/Toronto", 3, 8, 6, 59, 1, 59, "EST");
    check("America/Toronto", 3, 8, 7, 0, 3, 0, "EDT");
    check("America/Toronto", 11, 1, 5, 59, 1, 59, "EDT");
    check("America/Toronto", 11, 1, 6, 0, 1, 0, "EST");
    check("US/Pacific", 11, 19, 17, 30, 9, 30, "PST");
    check("Europe/Paris", 7, 1, 12, 0, 14, 0, "CEST");
    check("Asia/Kathmandu", 11, 19, 17, 30, 23, 15, "+0545");
    check("Australia/Lord_Howe", 7, 1, 12, 0, 22, 30, "+1030");
    check("Australia/Lord_Howe", 1, 1, 12, 0, 23, 0, "+11");
    check("Africa/Casablanca", 3, 1, 12, 0, 12, 0, "+00");
    check("Africa/Casablanca", 7, 1, 12, 0, 13, 0, "+01");
    check("UTC", 11, 19, 17, 30, 17, 30, "UTC");
    assert(!findClockZone("Unsupported/Zone"));

    tm utc{}, local{};
    utc.tm_year = 2026 - 1900;
    utc.tm_mon = 10;
    utc.tm_mday = 19;
    utc.tm_hour = 23;
    clockLocalTime(timegm(&utc), *findClockZone("Asia/Kathmandu"), local);
    assert(local.tm_mday == 20);
}
