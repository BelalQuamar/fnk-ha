#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <vector>
#include <cstdlib>
#include <memory>

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
    bool pressable() const;
};

struct CameraImage {
    std::unique_ptr<uint8_t, decltype(&std::free)> bytes{nullptr, std::free};
    size_t size = 0;
};

class HomeAssistant {
public:
    explicit HomeAssistant(const Settings& settings) : config(settings) {}
    bool readCamera(const String& id, CameraImage& image);
    bool readTimezone(String& zone);
    bool discover(std::vector<PowerItem>& items, IPAddress& serverIp);
    bool command(const PowerItem& item, bool on);
    bool refreshState(PowerItem& item);
    String error;

private:
    const Settings& config;
};
