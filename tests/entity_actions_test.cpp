#include "entity_actions.h"
#include <cassert>
#include <initializer_list>

int main() {
    using namespace EntityActions;
    for (const char* domain : {"button", "input_button"}) {
        assert(kind(domain) == Kind::Press);
        assert(std::strcmp(service(kind(domain), false), "press") == 0);
        assert(std::strcmp(service(kind(domain), true), "press") == 0);
    }
    for (const char* domain : {"light", "switch", "fan", "input_boolean", "humidifier"}) {
        assert(kind(domain) == Kind::Toggle);
        assert(std::strcmp(service(kind(domain), false), "turn_off") == 0);
        assert(std::strcmp(service(kind(domain), true), "turn_on") == 0);
    }
    for (const char* domain : {"sensor", "binary_sensor", "", "buttons"}) {
        assert(kind(domain) == Kind::ReadOnly);
        assert(service(kind(domain), true) == nullptr);
    }
}
