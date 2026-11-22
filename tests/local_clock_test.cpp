#include "local_clock.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <cstring>

void check(int month, int day, int utcHour, int utcMinute, int hour, int minute, const char* zone) {
    tm utc{};
    utc.tm_year = 2026-1900; utc.tm_mon = month-1; utc.tm_mday = day;
    utc.tm_hour = utcHour; utc.tm_min = utcMinute;
    time_t epoch = timegm(&utc);
    tm local; localtime_r(&epoch,&local);
    assert(local.tm_hour == hour && local.tm_min == minute);
    char text[8]; strftime(text,sizeof(text),"%Z",&local);
    assert(std::strcmp(text,zone)==0);
}
int main() {
    setenv("TZ",localTimezone,1); tzset();
    check(3,8,6,59,1,59,"EST");
    check(3,8,7,0,3,0,"EDT");
    check(11,1,5,59,1,59,"EDT");
    check(11,1,6,0,1,0,"EST");
    check(11,19,17,30,12,30,"EST");
}
