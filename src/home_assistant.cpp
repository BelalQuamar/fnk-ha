#include "home_assistant.h"
#include "entity_actions.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <algorithm>

bool Settings::valid() const {
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64 ||
        token.isEmpty() || token.length() > 2048 || !url.startsWith("http://") ||
        url.length() > 255) return false;

    // The ESP32 connects directly to a LAN hostname or IPv4 address and optional port.
    const int colon = url.indexOf(':', 7);
    const unsigned hostEnd = colon < 0 ? url.length() : colon;
    if (hostEnd == 7) return false;
    for (unsigned i = 7; i < hostEnd; ++i) {
        const char c = url[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
    }
    if (colon >= 0) {
        const String port = url.substring(colon + 1);
        if (port.isEmpty() || port.length() > 5) return false;
        for (unsigned i = 0; i < port.length(); ++i)
            if (!isdigit(static_cast<unsigned char>(port[i]))) return false;
        if (port.toInt() < 1 || port.toInt() > 65535) return false;
    }
    for (unsigned i = 0; i < token.length(); ++i) {
        const char c = token[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') return false;
    }
    return true;
}

bool PowerItem::controllable() const {
    return EntityActions::kind(domain.c_str()) == EntityActions::Kind::Toggle;
}

bool PowerItem::pressable() const {
    return EntityActions::kind(domain.c_str()) == EntityActions::Kind::Press;
}

namespace {
struct Request {
    WiFiClient client;
    HTTPClient http;

    bool send(const Settings& settings, const String& path, String& error,
              const String& body = String()) {
        // HTTP/1.0 keeps the stream free of chunk framing.
        http.useHTTP10(true);
        http.setConnectTimeout(3500);
        http.setTimeout(4000);
        if (!http.begin(client, settings.url + path)) {
            error = "Invalid Home Assistant URL";
            return false;
        }
        http.addHeader("Authorization", "Bearer " + settings.token);
        if (!body.isEmpty()) http.addHeader("Content-Type", "application/json");
        const int code = body.isEmpty() ? http.GET() : http.POST(body);
        if (code == 200) return true;
        if (code == 401 || code == 403) error = "HA token rejected - USB setup";
        else if (code <= 0) error = "Home Assistant unreachable";
        else error = "Home Assistant HTTP " + String(code);
        return false;
    }
};

template <typename Reader>
bool readItem(Reader& stream, PowerItem& item) {
    StaticJsonDocument<192> filter;
    filter["entity_id"] = true;
    filter["state"] = true;
    filter["attributes"]["friendly_name"] = true;
    StaticJsonDocument<2048> json;
    if (deserializeJson(json, stream, DeserializationOption::Filter(filter))) return false;

    const char* id = json["entity_id"];
    const char* state = json["state"];
    if (!id || !state) return false;
    item.id = id;
    const int dot = item.id.indexOf('.');
    if (dot < 1 || dot + 1 == item.id.length()) return false;
    for (unsigned i = 0; i < item.id.length(); ++i) {
        const char c = item.id[i];
        if (i != static_cast<unsigned>(dot) && !(c >= 'a' && c <= 'z') &&
            !(c >= '0' && c <= '9') && c != '_') return false;
    }
    item.domain = item.id.substring(0, dot);
    item.name = json["attributes"]["friendly_name"] | "";
    if (item.name.isEmpty()) item.name = item.id;
    item.state = state;
    item.online = item.state != "unavailable";
    item.known = item.state == "on" || item.state == "off";
    item.on = item.state == "on";
    return true;
}

// Cache one byte so discovery can distinguish an empty array without consuming
// the opening brace that ArduinoJson needs. All reads use the stream timeout.
class EntityReader {
public:
    explicit EntityReader(Stream& stream) : input(stream) {}

    int read() {
        if (pending >= 0) {
            const int value = pending;
            pending = -1;
            return value;
        }
        char value;
        return input.readBytes(&value, 1) == 1 ? static_cast<unsigned char>(value) : -1;
    }

    size_t readBytes(char* buffer, size_t length) {
        size_t count = 0;
        while (count < length) {
            const int value = read();
            if (value < 0) break;
            buffer[count++] = value;
        }
        return count;
    }

    int token() {
        int value;
        do { value = read(); } while (value >= 0 && isspace(value));
        return value;
    }

    void putBack(int value) { pending = value; }

private:
    Stream& input;
    int pending = -1;
};
}

bool HomeAssistant::readCamera(const String& id, CameraImage& image) {
    Request request;
    if (!request.send(config, "/api/camera_proxy/" + id + "?width=230&height=218", error))
        return false;

    const int size = request.http.getSize();
    if (size < 4 || size > 96 * 1024) {
        error = "Camera image size unsupported";
        return false;
    }
    CameraImage received;
    received.bytes.reset(static_cast<uint8_t*>(std::malloc(size)));
    if (!received.bytes) {
        error = "Not enough camera memory";
        return false;
    }
    auto* stream = request.http.getStreamPtr();
    if (!stream || stream->readBytes(received.bytes.get(), size) != static_cast<size_t>(size)) {
        error = "Incomplete camera image";
        return false;
    }
    if (received.bytes.get()[0] != 0xff || received.bytes.get()[1] != 0xd8) {
        error = "Camera needs JPEG snapshots";
        return false;
    }
    received.size = size;
    image = std::move(received);
    return true;
}

bool HomeAssistant::readTimezone(String& zone) {
    Request request;
    String requestError;
    if (!request.send(config, "/api/config", requestError)) return false;
    StaticJsonDocument<96> filter;
    filter["time_zone"] = true;
    StaticJsonDocument<512> json;
    if (deserializeJson(json, request.http.getStream(), DeserializationOption::Filter(filter))) return false;
    zone = json["time_zone"] | "";
    return !zone.isEmpty();
}

bool HomeAssistant::discover(std::vector<PowerItem>& items, IPAddress& serverIp) {
    Request request;
    if (!request.send(config, "/api/states", error)) return false;
    auto* stream = request.http.getStreamPtr();
    error = "Incomplete HA entity list";
    if (!stream) return false;
    serverIp = stream->remoteIP();
    EntityReader reader(*stream);
    if (reader.token() != '[') return false;

    // Parse one filtered object at a time, regardless of the total JSON size.
    std::vector<PowerItem> found;
    int token = reader.token();
    if (token != ']') {
        for (;;) {
            if (token != '{') return false;
            reader.putBack(token);
            PowerItem item;
            if (!readItem(reader, item)) return false;
            found.push_back(std::move(item));
            token = reader.token();
            if (token == ']') break;
            if (token != ',') return false;
            token = reader.token();
        }
    }
    std::sort(found.begin(), found.end(), [](const PowerItem& a, const PowerItem& b) {
        const int order = a.name.compareTo(b.name);
        return order == 0 ? a.id < b.id : order < 0;
    });
    items.swap(found);
    error = "";
    return true;
}

bool HomeAssistant::command(const PowerItem& item, bool on) {
    const char* service = EntityActions::service(EntityActions::kind(item.domain.c_str()), on);
    if (!service) return false;
    Request request;
    return request.send(config, "/api/services/" + item.domain + "/" + service,
                        error, "{\"entity_id\":\"" + item.id + "\"}");
}

bool HomeAssistant::refreshState(PowerItem& item) {
    Request request;
    if (!request.send(config, "/api/states/" + item.id, error)) return false;
    PowerItem updated;
    if (!readItem(request.http.getStream(), updated) || updated.id != item.id) {
        error = "Invalid HA entity state";
        return false;
    }
    item = std::move(updated);
    return true;
}
