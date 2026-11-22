#pragma once
#include <stdint.h>

class DisplayIdle {
public:
    static constexpr uint32_t timeoutMs = 2UL * 60UL * 1000UL;
    explicit DisplayIdle(uint32_t now = 0) : lastActivity(now) {}
    bool activity(uint32_t now) {
        const bool waking = asleep;
        lastActivity = now; asleep = false;
        return waking;
    }
    bool sleepDue(uint32_t now) const {
        return !asleep && static_cast<uint32_t>(now-lastActivity) >= timeoutMs;
    }
    void sleep() { asleep = true; }
    bool isAsleep() const { return asleep; }
private:
    uint32_t lastActivity;
    bool asleep = false;
};
