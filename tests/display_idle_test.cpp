#include "display_idle.h"
#include <cassert>
#include <stdint.h>
int main() {
    DisplayIdle timer(100);
    assert(!timer.sleepDue(120099));
    assert(timer.sleepDue(120100));
    timer.sleep();
    assert(timer.isAsleep());
    assert(!timer.sleepDue(200000));
    assert(timer.activity(200000)); // This first interaction is wake-only.
    assert(!timer.isAsleep());
    assert(!timer.activity(200001)); // Subsequent interaction may control a device.
    assert(!timer.sleepDue(320000));
    assert(timer.sleepDue(320001));
    assert(!timer.activity(319999)); // Activity resets the inactivity countdown.
    assert(!timer.sleepDue(320001));
    DisplayIdle rollover(UINT32_MAX-100);
    assert(!rollover.sleepDue(119898));
    assert(rollover.sleepDue(119899));
}
