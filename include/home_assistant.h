#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <vector>

struct Settings {
    String ssid, password, url, token;
    bool valid() const;
};

struct PowerItem {
    String id, name, domain, state;
    bool online = false;
    bool known = false;
    bool on = false;
    bool sent = false;

    bool controllable() const;
};

class HomeAssistant {
public:
    explicit HomeAssistant(const Settings& settings) : config(settings) {}
    bool readTimezone(String& zone);
    bool discover(std::vector<PowerItem>& items, IPAddress& serverIp);
    bool command(const PowerItem& item, bool on);
    bool refreshState(PowerItem& item);
    String error;

private:
    const Settings& config;
};
