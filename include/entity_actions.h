#pragma once

#include <cstring>

namespace EntityActions {
enum class Kind { ReadOnly, Toggle, Press };

inline Kind kind(const char* domain) {
    if (std::strcmp(domain, "button") == 0 || std::strcmp(domain, "input_button") == 0)
        return Kind::Press;
    if (std::strcmp(domain, "light") == 0 || std::strcmp(domain, "switch") == 0 ||
        std::strcmp(domain, "fan") == 0 || std::strcmp(domain, "input_boolean") == 0 ||
        std::strcmp(domain, "humidifier") == 0)
        return Kind::Toggle;
    return Kind::ReadOnly;
}

inline const char* service(Kind action, bool on) {
    switch (action) {
        case Kind::Press: return "press";
        case Kind::Toggle: return on ? "turn_on" : "turn_off";
        default: return nullptr;
    }
}
}
