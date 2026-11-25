#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include "home_assistant.h"
#include "ui_layout.h"
#include "display_idle.h"
#include "local_clock.h"
#include <time.h>
#include <algorithm>

TFT_eSPI screen;
TFT_eSprite canvas(&screen);
Settings settings;
IPAddress homeAssistantIp;
constexpr const char* firmwareVersion = FNK_HA_VERSION;
const ClockZone* clockZone = findClockZone("UTC");
SemaphoreHandle_t stateLock;
QueueHandle_t actions;
std::vector<PowerItem> items;
std::vector<size_t> viewItems;
std::vector<String> favorites;
enum class View : uint8_t { Controls, All, Favorites, Settings, Cameras, Count };
View view = View::Controls;
const char* viewLabels[] = {"CONTROLS", "ALL", "FAVORITES", "SETTINGS", "CAMERAS"};
unsigned layoutRevision = 0;
CameraImage cameraImage;
String cameraImageId, cameraError;
bool cameraPaused = false, cameraRendered = false;
constexpr unsigned long cameraIntervalMs = 1200;
bool scrollingNames = false;
unsigned long scrollStarted = 0;
constexpr unsigned long holdMs = 650;
String notice = "Starting...";
bool demo = true, busy = false, touchAvailable = false, dirty = true;
int focus = 0;
int page = 0;
unsigned renderCount = 0;
unsigned discoveredCount = 0;
bool catalogLive = false, layoutDrawn = false;
const uint16_t BG = 0x2108, CARD = 0x4208, MUTED = 0x9CF3;
DisplayIdle displayIdle;
constexpr size_t setupLineLimit = 3500;
void draw();
bool cameraView() { return view == View::Cameras; }

const PowerItem* selectedCamera() {
    return cameraView() && page >= 0 && static_cast<size_t>(page) < viewItems.size()
        ? &items[viewItems[page]] : nullptr;
}

void sleepDisplay() {
    if (displayIdle.isAsleep()) return;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    cameraPaused = true;
    xSemaphoreGive(stateLock);
    digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
    screen.writecommand(TFT_DISPOFF);
    screen.writecommand(ILI9341_SLPIN);
    delay(120);
    displayIdle.sleep();
}

// Returns true for a wake gesture: consume it rather than operating a device.
bool displayActivity() {
    bool waking = displayIdle.activity(millis());
    if (waking) {
        xSemaphoreTake(stateLock, portMAX_DELAY);
        cameraPaused = false;
        xSemaphoreGive(stateLock);
        screen.writecommand(ILI9341_SLPOUT);
        delay(120);
        screen.writecommand(TFT_DISPON);
        draw();
        digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    }
    return waking;
}

struct Action { int index; bool on; }; // -1 refresh

void status(const String& text, bool working) {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    notice = text; busy = working; dirty = true;
    xSemaphoreGive(stateLock);
    Serial.println(text); // Never log credentials, tokens, or full API responses.
}

void invalidateLayout() {
    layoutDrawn = false;
    ++layoutRevision;
    scrollStarted = millis();
    dirty = true;
}

// Called with stateLock held once the network task starts.
void rebuildView() {
    viewItems.clear();
    if (view == View::Favorites) {
        for (const auto& id : favorites) {
            auto item = std::find_if(items.begin(), items.end(), [&](const PowerItem& entry) {
                return entry.id == id;
            });
            if (item != items.end()) viewItems.push_back(item - items.begin());
        }
    } else if (view != View::Settings) {
        for (size_t i = 0; i < items.size(); ++i)
            if (cameraView() ? items[i].domain == "camera" :
                               view == View::All || items[i].controllable()) viewItems.push_back(i);
    }
    page = UI::clampPage(page, viewItems.size(), cameraView());
    if (focus >= UI::focusCount(viewItems.size(), page, cameraView())) focus = 0;
}

bool saveFavorites(const std::vector<String>& next) {
    String stored;
    for (const auto& id : next) {
        if (stored.length() + id.length() + 1 > 3500 ||
            !stored.concat(id) || !stored.concat('\n')) return false;
    }
    Preferences prefs;
    if (!prefs.begin("fnk-ha", false)) return false;
    if (stored.isEmpty()) return !prefs.isKey("favorites") || prefs.remove("favorites");
    return prefs.putString("favorites", stored) == stored.length();
}

Settings readSettings(JsonObjectConst json) {
    Settings result;
    result.ssid = json["ssid"] | "";
    result.password = json["password"] | "";
    result.url = json["url"] | "";
    while (result.url.endsWith("/")) result.url.remove(result.url.length() - 1);
    result.token = json["token"] | "";
    return result;
}

void loadSettings() {
    Preferences prefs;
    if (!prefs.begin("fnk-ha", true)) return;
    const uint8_t savedView = prefs.getUChar("view", 0);
    if (savedView < static_cast<uint8_t>(View::Count)) view = static_cast<View>(savedView);
    const String stored = prefs.isKey("favorites") ? prefs.getString("favorites") : String();
    for (int start = 0, end; (end = stored.indexOf('\n', start)) >= 0; start = end + 1)
        favorites.push_back(stored.substring(start, end));
    DynamicJsonDocument json(4096);
    if (!deserializeJson(json, prefs.getString("config"))) {
        settings = readSettings(json.as<JsonObjectConst>());
    }
    if (settings.valid()) {
        const int port = settings.url.indexOf(':', 7);
        homeAssistantIp.fromString((port < 0 ? settings.url.substring(7) : settings.url.substring(7, port)).c_str());
    }
    if (prefs.isKey("clockZone")) {
        const String stored = prefs.getString("clockZone");
        const int separator = stored.indexOf('\n');
        if (separator > 0 && stored.substring(0, separator) == settings.url) {
            const auto* saved = findClockZone(stored.c_str() + separator + 1);
            if (saved) clockZone = saved;
        }
    }
}
void updateClockZone(const String& name) {
    const auto* found = findClockZone(name.c_str());
    if (!found) return;

    xSemaphoreTake(stateLock, portMAX_DELAY);
    const bool changed = clockZone != found;
    clockZone = found;
    dirty = dirty || changed;
    xSemaphoreGive(stateLock);

    Preferences prefs;
    if (prefs.begin("fnk-ha", false)) {
        const String value = settings.url + "\n" + name;
        if (!prefs.isKey("clockZone") || prefs.getString("clockZone") != value)
            prefs.putString("clockZone", value);
    }
}

void provision(const String& line) {
    DynamicJsonDocument json(4096);
    if (deserializeJson(json, line)) { Serial.println("SETUP_ERROR invalid JSON"); return; }
    if (String(json["action"] | "") == "display_sleep") {
        sleepDisplay(); Serial.println("DISPLAY_ASLEEP"); return;
    }
    if (String(json["action"] | "") == "display_wake") {
        displayActivity(); Serial.println("DISPLAY_AWAKE"); return;
    }
    if (String(json["action"] | "") == "screenshot") {
        // Export the rendered framebuffer for visual verification over USB.
        xSemaphoreTake(stateLock, portMAX_DELAY);
        Serial.println("SCREENSHOT 240 320 RGB565_LE");
        uint8_t row[480];
        for (int y = 0; y < 320; ++y) {
            for (int x = 0; x < 240; ++x) {
                uint16_t pixel = canvas.readPixel(x, y);
                row[x * 2] = pixel & 255; row[x * 2 + 1] = pixel >> 8;
            }
            Serial.write(row, sizeof(row));
        }
        xSemaphoreGive(stateLock); return;
    }
    if (String(json["action"] | "") == "info") {
        json.clear(); json["app"] = "FNK_HA"; json["demo"] = demo;
        json["touch"] = touchAvailable;
        json["free_heap"] = ESP.getFreeHeap();
        json["display_asleep"] = displayIdle.isAsleep();
        json["backlight_on"] = digitalRead(TFT_BL) == TFT_BACKLIGHT_ON;
        json["display_timeout_seconds"] = DisplayIdle::timeoutMs / 1000;
        time_t now = time(nullptr);
        json["clock_synced"] = now >= firstSynchronizedTime;
        xSemaphoreTake(stateLock, portMAX_DELAY);
        tm local{};
        const char* abbreviation = clockLocalTime(now, *clockZone, local);
        char localText[32];
        strftime(localText, sizeof(localText), "%Y-%m-%d %H:%M:%S", &local);
        json["local_time"] = String(localText) + " " + abbreviation;
        json["timezone"] = clockZone->name;
        json["channels"] = viewItems.size();
        json["view"] = viewLabels[static_cast<uint8_t>(view)]; json["favorites"] = favorites.size(); json["status"] = notice;
        json["ui_version"] = 17; json["font2_loaded"] = true;
        json["firmware_version"] = firmwareVersion;
        const bool connected = WiFi.status() == WL_CONNECTED;
        json["wifi_connected"] = connected;
        json["device_ip"] = connected ? WiFi.localIP().toString() : String();
        json["ha_connected"] = connected && catalogLive;
        json["camera_frame_bytes"] = cameraImage.size;
        json["camera_rendered"] = cameraRendered;
        json["camera_status"] = cameraError;
        json["ha_ip"] = homeAssistantIp != IPAddress() ? homeAssistantIp.toString() : String();
        json["devices_per_page"] = UI::devicesPerPage;
        json["page"] = page+1; json["pages"] = UI::pageCount(viewItems.size(), cameraView());
        json["auto_refresh_seconds"] = UI::refreshIntervalMs / 1000;
        json["discovered_channels"] = discoveredCount;
        json["renders"] = renderCount; json["redraw_pending"] = dirty;
        json["frame_width"] = canvas.width(); json["frame_height"] = canvas.height();
        xSemaphoreGive(stateLock);
        serializeJson(json, Serial); Serial.println(); return;
    }
    if (String(json["action"] | "") != "configure") { Serial.println("SETUP_ERROR unknown action"); return; }
    if (!readSettings(json.as<JsonObjectConst>()).valid()) {
        Serial.println("SETUP_ERROR invalid Wi-Fi, HA URL, or token"); return;
    }
    Preferences prefs;
    if (!prefs.begin("fnk-ha", false)) { Serial.println("SETUP_ERROR storage unavailable"); return; }
    const bool saved = prefs.putString("config", line) == line.length();
    prefs.end();
    if (!saved) { Serial.println("SETUP_ERROR could not save"); return; }
    Serial.println("SETUP_SAVED"); Serial.flush(); delay(250); ESP.restart();
}

void updateCamera(HomeAssistant& ha, String& lastId, unsigned long& lastRequest) {
    String id;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    const auto* camera = selectedCamera();
    if (camera && camera->online && catalogLive && !cameraPaused && !busy) id = camera->id;
    xSemaphoreGive(stateLock);
    if (id.isEmpty()) {
        lastId = "";
        return;
    }
    const unsigned long now = millis();
    if (id == lastId && now - lastRequest < cameraIntervalMs) return;
    lastId = id;
    lastRequest = now;

    CameraImage frame;
    const bool received = ha.readCamera(id, frame);
    xSemaphoreTake(stateLock, portMAX_DELAY);
    camera = selectedCamera();
    if (camera && camera->id == id && !cameraPaused) {
        if (received || cameraImageId != id) cameraImage = std::move(frame);
        cameraImageId = id;
        cameraError = received ? "" : ha.error;
        dirty = true;
    }
    xSemaphoreGive(stateLock);
}

void worker(void*) {
    HomeAssistant ha(settings);
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    status("Connecting to Wi-Fi...", true);
    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 25000) vTaskDelay(pdMS_TO_TICKS(200));
    bool clockStarted = false, wasConnected = false;
    Action action{-1, false};
    bool first = true;
    unsigned long lastDiscovery = millis(), lastCameraRequest = 0;
    String lastCameraId;
    for (;;) {
        const bool connected = WiFi.status() == WL_CONNECTED;
        const bool reconnected = connected && !wasConnected;
        wasConnected = connected;
        if (connected && !clockStarted) {
            configTzTime("UTC0", ntpPrimary, ntpSecondary);
            clockStarted = true;
        }
        if (!first && xQueueReceive(actions, &action, pdMS_TO_TICKS(100)) != pdTRUE) {
            // Reserve discovery under the same lock as user actions.
            xSemaphoreTake(stateLock, portMAX_DELAY);
            if (!connected && catalogLive) {
                catalogLive = false;
                notice = "Wi-Fi offline";
                dirty = true;
            }
            const bool automatic = connected && !busy &&
                (reconnected || UI::discoveryDue(millis(), lastDiscovery));
            if (automatic) busy = true;
            xSemaphoreGive(stateLock);
            if (!automatic) {
                updateCamera(ha, lastCameraId, lastCameraRequest);
                continue;
            }
            action = {-1, false};
        }
        first = false;
        if (!connected) {
            status("Wi-Fi offline", false);
            continue;
        }
        if (action.index == -1) {
            status("Loading HA entities...", true);
            std::vector<PowerItem> found;
            IPAddress serverIp;
            if (ha.discover(found, serverIp)) {
                String zone;
                if (ha.readTimezone(zone)) updateClockZone(zone);
                const unsigned total = found.size();
                xSemaphoreTake(stateLock, portMAX_DELAY);
                if (items.size() != found.size() ||
                    !std::equal(items.begin(), items.end(), found.begin(), [](const PowerItem& a, const PowerItem& b) {
                        return a.id == b.id && a.name == b.name && a.state == b.state;
                    })) invalidateLayout();
                items.swap(found);
                if (serverIp != IPAddress()) homeAssistantIp = serverIp;
                rebuildView();
                catalogLive = true;
                discoveredCount = total;
                xSemaphoreGive(stateLock);
                status("", false);
            } else {
                xSemaphoreTake(stateLock, portMAX_DELAY);
                catalogLive = false;
                xSemaphoreGive(stateLock);
                status(ha.error, false);
            }
            lastDiscovery = millis();
            lastCameraId = "";
            continue;
        }
        status("Sending command...", true);
        const size_t index = action.index;
        xSemaphoreTake(stateLock, portMAX_DELAY);
        PowerItem item = items[index];
        items[index].sent = true;
        dirty = true;
        xSemaphoreGive(stateLock);

        const bool accepted = ha.command(item, action.on);
        if (item.pressable()) {
            xSemaphoreTake(stateLock, portMAX_DELAY);
            items[index].sent = false;
            xSemaphoreGive(stateLock);
            // A stateless press acknowledges the command, with no power state to confirm.
            status(accepted ? String("Button press accepted") : ha.error, false);
            continue;
        }
        bool confirmed = false;
        if (accepted) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                vTaskDelay(pdMS_TO_TICKS(600));
                if (ha.refreshState(item) && item.known && item.online && item.on == action.on) {
                    confirmed = true;
                    break;
                }
            }
        }
        item.sent = false;
        if (!confirmed) item.known = false;
        xSemaphoreTake(stateLock, portMAX_DELAY);
        items[index] = item;
        dirty = true;
        xSemaphoreGive(stateLock);
        if (!accepted) status(ha.error, false);
        else status(confirmed ? "" : "Unconfirmed - tap Refresh", false);
    }
}

String shortText(String value, size_t length) {
    if (value.length() > length) return value.substring(0, length - 1) + "~";
    return value;
}

void label(const String& value, int x, int y, int font, uint16_t color) {
    canvas.setTextDatum(TL_DATUM); canvas.setTextColor(color, BG);
    canvas.drawString(value, x, y, font);
}
void button(int id, UI::Rect rect, uint16_t color, bool enabled = true, const char* text = "") {
    const int cx = rect.x + rect.w / 2, cy = rect.y + rect.h / 2;
    const uint16_t fill = enabled ? color : CARD;
    const uint16_t ink = enabled ? TFT_WHITE : MUTED;
    canvas.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 8, fill);
    if (!touchAvailable && id == UI::focusedControl(focus, viewItems.size(), page, cameraView()))
        canvas.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_WHITE);

    if (id == UI::refresh) {
        canvas.drawSmoothArc(cx - 1, cy, 8, 6, 325, 270, ink, fill);
        canvas.fillTriangle(cx + 3, cy - 1, cx + 11, cy - 1, cx + 7, cy + 4, ink);
    } else if (id == UI::previous || id == UI::next) {
        const int direction = id == UI::next ? 1 : -1;
        canvas.drawWideLine(cx - direction*4, cy - 6, cx + direction*3, cy, 3, ink, fill);
        canvas.drawWideLine(cx + direction*3, cy, cx - direction*4, cy + 6, 3, ink, fill);
    } else {
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(ink);
        canvas.drawString(text, cx, cy, canvas.textWidth(text, 2) <= rect.w - 10 ? 2 : 1);
    }
}

void drawNameLine(const String& text, UI::Rect line, int font) {
    const int width = canvas.textWidth(text, font);
    canvas.setTextDatum(TL_DATUM);
    if (width <= line.w) {
        canvas.drawString(text, line.x, line.y, font);
        return;
    }

    scrollingNames = true;
    const unsigned long elapsed = millis() - scrollStarted;
    const int period = width + 28;
    const int offset = ((elapsed < 800 ? 0 : elapsed - 800) / 35) % period;
    canvas.setViewport(line.x, line.y, line.w, line.h);
    canvas.drawString(text, -offset, 0, font);
    canvas.drawString(text, period - offset, 0, font);
    canvas.resetViewport();
}

void deviceName(const String& name, UI::Rect area, uint16_t background) {
    const int font = area.w >= 190 && canvas.textWidth(name, 4) <= area.w ? 4 : 2;
    const int lineHeight = canvas.fontHeight(font);
    const int width = canvas.textWidth(name, font);
    canvas.setTextColor(TFT_WHITE, background);
    if (width <= area.w) {
        drawNameLine(name, {area.x, area.y + (area.h - lineHeight) / 2, area.w, lineHeight}, font);
        return;
    }

    // Prefer a word boundary that balances the width of the two rows.
    int split = 0;
    int smallestDifference = width;
    for (int space = name.indexOf(' '); space >= 0; space = name.indexOf(' ', space + 1)) {
        if (space == 0 || space + 1 >= name.length()) continue;
        const int left = canvas.textWidth(name.substring(0, space), font);
        const int right = canvas.textWidth(name.substring(space + 1), font);
        const int difference = abs(left - right);
        if (difference < smallestDifference) {
            split = space;
            smallestDifference = difference;
        }
    }

    // An unbroken name still gets two rows. Never split inside a UTF-8 character.
    if (split == 0) {
        for (unsigned i = 1; i < name.length(); ++i) {
            if ((static_cast<uint8_t>(name[i]) & 0xC0) == 0x80) continue;
            const int left = canvas.textWidth(name.substring(0, i), font);
            const int right = canvas.textWidth(name.substring(i), font);
            const int difference = abs(left - right);
            if (difference < smallestDifference) {
                split = i;
                smallestDifference = difference;
            }
        }
    }
    if (split == 0) {
        drawNameLine(name, {area.x, area.y + (area.h - lineHeight) / 2, area.w, lineHeight}, font);
        return;
    }

    String first = name.substring(0, split);
    String second = name.substring(split);
    first.trim();
    second.trim();
    const int rowGap = 2;
    const int top = area.y + (area.h - 2*lineHeight - rowGap) / 2;
    drawNameLine(first, {area.x, top, area.w, lineHeight}, font);
    drawNameLine(second, {area.x, top + lineHeight + rowGap, area.w, lineHeight}, font);
}

void deviceTile(const PowerItem& item, UI::Rect rect, bool focused) {
    constexpr uint16_t tileBackground = 0x1908, border = 0x528C;
    const bool online = item.online && (catalogLive || demo);
    const int radius = 9;
    canvas.fillRoundRect(rect.x, rect.y, rect.w, rect.h, radius, tileBackground);
    canvas.drawRoundRect(rect.x, rect.y, rect.w, rect.h, radius, focused ? TFT_WHITE : border);

    if (std::find(favorites.begin(), favorites.end(), item.id) != favorites.end())
        canvas.fillCircle(rect.x + rect.w - 9, rect.y + 8, 2, TFT_YELLOW);

    const int badgeX = rect.x + 9, badgeY = rect.y + rect.h - 27;
    deviceName(item.name, {badgeX, rect.y + 5, rect.w - 18, badgeY - rect.y - 8}, tileBackground);

    String state = item.state;
    uint16_t background = 0x2251, foreground = 0xD71F;
    if (!online) {
        state = "Offline"; background = 0x4208; foreground = 0xDEFB;
    } else if (item.sent || (item.controllable() && !item.known)) {
        state = item.sent ? "Updating" : "Unknown";
        background = 0xFCE7; foreground = TFT_BLACK;
    } else if (item.pressable()) {
        state = "Press";
    } else if (item.known) {
        state = item.on ? "On" : "Off";
        background = item.on ? 0x036A : 0x39E9;
        foreground = item.on ? 0xDFFA : 0xDEFB;
    }

    const int maxWidth = rect.w - 36;
    if (canvas.textWidth(state, 2) > maxWidth) {
        while (!state.isEmpty() && canvas.textWidth(state + "..", 2) > maxWidth)
            state.remove(state.length() - 1);
        state += "..";
    }
    canvas.fillRoundRect(badgeX, badgeY, canvas.textWidth(state, 2) + 18, 20, 7, background);
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(foreground);
    canvas.drawString(state, badgeX + 9, badgeY + 10, 2);
}

void settingsCard(int y, int height, const char* title) {
    canvas.fillRoundRect(5, y, 230, height, 8, CARD);
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(title, 14, y + 8, 2);
}

void settingsValue(const String& text, int y, bool prominent = false) {
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextColor(prominent ? TFT_WHITE : 0xD69A);
    const int font = prominent && canvas.textWidth(text, 2) <= 208 ? 2 : 1;
    canvas.setViewport(14, y, 208, canvas.fontHeight(font));
    canvas.drawString(text, 0, 0, font);
    canvas.resetViewport();
}

void settingsConnection(const char* text, int y, bool connected) {
    canvas.setTextDatum(TR_DATUM);
    canvas.setTextColor(connected ? 0x9FF5 : 0xFE36);
    canvas.drawString(text, 226, y, 1);
}

void drawSettings() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool haConnected = connected && catalogLive;
    label("Settings", 8, 39, 4, TFT_WHITE);

    settingsCard(70, 66, "Wi-Fi");
    settingsConnection(demo ? "NOT SET UP" : connected ? "CONNECTED" : "DISCONNECTED", 82, connected);
    settingsValue(settings.ssid.isEmpty() ? "No Wi-Fi configured" : settings.ssid, 96, true);
    settingsValue(connected ? "Device IP  " + WiFi.localIP().toString() : "Device IP  --", 120);
    if (connected) {
        canvas.setTextDatum(TR_DATUM);
        canvas.setTextColor(0xD69A);
        canvas.drawString(String(WiFi.RSSI()) + " dBm", 226, 120, 1);
    }

    settingsCard(142, 66, "Home Assistant");
    settingsConnection(demo ? "NOT SET UP" : haConnected ? "CONNECTED" :
                       !connected ? "WI-FI OFFLINE" : busy ? "CHECKING" : "UNAVAILABLE", 154, haConnected);
    settingsValue(homeAssistantIp != IPAddress() ? "Server IP  " + homeAssistantIp.toString() : "Server IP  --", 168, true);
    settingsValue(settings.url.isEmpty() ? "No server configured" : settings.url, 191);

    settingsCard(214, 67, "Device");
    settingsValue(String("Firmware  ") + firmwareVersion, 240);
    settingsValue(String("Timezone  ") + clockZone->name, 261);
}

bool cameraBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t* pixels) {
    canvas.pushImage(x, y, width, height, pixels);
    return true;
}

void drawCamera() {
    const auto* camera = selectedCamera();
    if (!camera) {
        label(busy ? "Finding cameras..." : "No cameras found", 12, 110, 2, MUTED);
        return;
    }

    const int nameFont = canvas.textWidth(camera->name, 2) <= 220 ? 2 : 1;
    canvas.setViewport(8, 39, 224, canvas.fontHeight(nameFont));
    label(camera->name, 0, 0, nameFont, TFT_WHITE);
    canvas.resetViewport();

    constexpr UI::Rect preview{5, 59, 230, 210};
    canvas.fillRect(preview.x, preview.y, preview.w, preview.h, TFT_BLACK);
    const char* statusText = "Loading camera...";
    if (!camera->online || !catalogLive) {
        statusText = "Camera offline";
    } else if (cameraImageId == camera->id) {
        if (cameraImage.size) {
            uint16_t width = 0, height = 0;
            if (TJpgDec.getJpgSize(&width, &height, cameraImage.bytes.get(), cameraImage.size) == JDR_OK) {
                int scale = 1;
                while (scale < 8 && (width / scale > preview.w || height / scale > preview.h)) scale *= 2;
                TJpgDec.setJpgScale(scale);
                canvas.setViewport(preview.x, preview.y, preview.w, preview.h, false);
                canvas.setSwapBytes(true);
                const auto result = TJpgDec.drawJpg(preview.x + (preview.w - width / scale) / 2,
                    preview.y + (preview.h - height / scale) / 2, cameraImage.bytes.get(), cameraImage.size);
                canvas.setSwapBytes(false);
                canvas.resetViewport();
                if (result == JDR_OK) {
                    cameraRendered = true;
                    label(cameraError.isEmpty() ? "Live preview" : "Preview stalled - retrying", 8, 273, 1, MUTED);
                    return;
                }
            }
            statusText = "Unsupported camera JPEG";
        } else if (!cameraError.isEmpty()) {
            statusText = cameraError.c_str();
        }
    }
    label(shortText(statusText, 36), 12, 152, 1, MUTED);
}

void draw() {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    dirty = false;
    ++renderCount;
    scrollingNames = false;
    cameraRendered = false;
    canvas.fillSprite(BG);
    const time_t now = time(nullptr);
    char clockText[12] = "--:--";
    char secondsText[4] = "";
    char dateText[16] = "";
    if (now >= firstSynchronizedTime) {
        tm local{};
        clockLocalTime(now, *clockZone, local);
        strftime(clockText, sizeof(clockText), "%H:%M", &local);
        strftime(secondsText, sizeof(secondsText), ":%S", &local);
        strftime(dateText, sizeof(dateText), "%d %b %Y", &local);
    }
    label(clockText, 8, 2, 4, TFT_WHITE);
    label(secondsText, 12 + canvas.textWidth(clockText, 4), 16, 1, MUTED);
    label(dateText, 108, 7, 2, TFT_WHITE);

    const int pages = UI::pageCount(viewItems.size(), cameraView());
    if (pages > 1) {
        button(UI::previous, UI::previousButton, CARD, page > 0);
        button(UI::next, UI::nextButton, CARD, page+1 < pages);
        canvas.setTextDatum(MC_DATUM); canvas.setTextColor(MUTED, BG);
        const String pageText = String(page+1) + "/" + pages;
        const auto indicator = UI::pageIndicator;
        canvas.drawString(pageText, indicator.x + indicator.w / 2, indicator.y + indicator.h / 2,
                          canvas.textWidth(pageText, 2) <= indicator.w ? 2 : 1);
    }
    if (view == View::Settings) {
        drawSettings();
    } else if (cameraView()) {
        drawCamera();
    } else {
        const int visible = UI::visibleCount(viewItems.size(), page);
        const String headerStatus = demo ? "DEMO - configure via USB" : busy ? "" : notice;
        if (!headerStatus.isEmpty()) label(shortText(headerStatus, 36), 8, 29, 1, TFT_CYAN);
        for (int slot = 0; slot < visible; ++slot) {
            size_t index = page*UI::devicesPerPage + slot;
            UI::Rect rect = UI::tile(slot, viewItems.size(), page);
            const bool focused = !touchAvailable &&
                static_cast<int>(index) == UI::focusedControl(focus, viewItems.size(), page, cameraView());
            deviceTile(items[viewItems[index]], rect, focused);
        }
        if (viewItems.empty()) {
            if (busy) label("Loading devices...", 20, 110, 2, MUTED);
            else if (view == View::Favorites) {
                label("No favorites yet", 20, 110, 2, MUTED);
                label("Hold a tile in All or Controls", 20, 140, 2, MUTED);
            } else label(view == View::Controls ? "No controls found" : "No devices loaded", 20, 110, 2, MUTED);
        }
    }
    button(UI::refresh, UI::refreshButton, 0x2250, !busy);
    button(UI::changeView, UI::viewButton, 0x2250, true, viewLabels[static_cast<uint8_t>(view)]);
    canvas.pushSprite(0, 0);
    layoutDrawn = true;
    xSemaphoreGive(stateLock);
}

// Called with stateLock held.
void activate(int id, bool hold = false) {
    if (!layoutDrawn || id == -1) return;
    if (id == UI::previous || id == UI::next) {
        const int nextPage = UI::clampPage(page + (id == UI::next ? 1 : -1), viewItems.size(), cameraView());
        if (nextPage == page) return;
        page = nextPage;
        if (cameraView()) {
            cameraImage = CameraImage{};
            cameraImageId = "";
            cameraError = "";
        }
        focus = 0;
        invalidateLayout();
        return;
    }
    if (id == UI::changeView) {
        const auto next = static_cast<uint8_t>((static_cast<uint8_t>(view) + 1) % static_cast<uint8_t>(View::Count));
        Preferences prefs;
        if (!prefs.begin("fnk-ha", false) || !prefs.putUChar("view", next)) {
            notice = "Could not save view"; dirty = true; return;
        }
        if (cameraView()) {
            cameraImage = CameraImage{};
            cameraImageId = "";
            cameraError = "";
        }
        view = static_cast<View>(next);
        page = focus = 0;
        rebuildView();
        invalidateLayout();
        return;
    }

    if (busy) return;
    Action action{-1, false};
    if (!cameraView() && id >= 0 && static_cast<size_t>(id) < viewItems.size()) {
        action.index = viewItems[id];
        const PowerItem& item = items[action.index];
        if (hold) {
            auto next = favorites;
            auto match = std::find(next.begin(), next.end(), item.id);
            if (view == View::Favorites) {
                if (match == next.end() || match == next.begin()) return;
                std::rotate(next.begin(), match, match + 1);
            } else if (match == next.end()) {
                next.push_back(item.id);
            } else {
                next.erase(match);
            }
            if (next == favorites) return;
            if (saveFavorites(next)) {
                favorites.swap(next);
                if (view == View::Favorites) page = focus = 0;
                rebuildView();
                notice = view == View::Favorites ? "Favorite moved to first" : "Favorites updated";
                invalidateLayout();
            } else {
                notice = "Could not save favorites";
                dirty = true;
            }
            return;
        }
        if ((!catalogLive && !demo) || !item.online) return;
        if (item.controllable()) {
            if (!item.known) action.index = -1;
            else action.on = !item.on;
        } else if (!item.pressable()) return;
    } else if (id != UI::refresh) return;

    if (demo) {
        if (action.index >= 0) {
            auto& item = items[action.index];
            item.on = action.on;
            item.state = action.on ? "on" : "off";
        }
        dirty = true;
    } else if (xQueueSend(actions, &action, 0) == pdTRUE) {
        busy = true; dirty = true;
    }
}

void setup() {
    // Native USB CDC defaults to 256 bytes, smaller than a setup message.
    Serial.setRxBufferSize(setupLineLimit + 2);
    Serial.begin(115200);
    stateLock = xSemaphoreCreateMutex(); actions = xQueueCreate(1, sizeof(Action));
    screen.init(); screen.setRotation(0);
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    displayIdle.activity(millis());
    canvas.setColorDepth(8);
    TJpgDec.setCallback(cameraBlock);
    if (!canvas.createSprite(240, 320)) {
        screen.fillScreen(TFT_BLACK); screen.setTextColor(TFT_WHITE);
        screen.drawString("Display memory error", 10, 100, 2);
        while (true) delay(1000);
    }
    pinMode(0, INPUT_PULLUP);
    pinMode(18, OUTPUT); digitalWrite(18, LOW); delay(10); digitalWrite(18, HIGH); delay(150);
    Wire.begin(16, 15); Wire.setTimeOut(30);
    Wire.beginTransmission(0x38); touchAvailable = Wire.endTransmission() == 0;
    loadSettings(); demo = !settings.valid();
    if (demo) {
        notice = "Configure with tools/configure.py";
        for (const char* name : {"Living room (demo)", "Desk lamp (demo)", "Smart plug (demo)"}) {
            PowerItem item; item.name = name; item.id = String("demo.") + items.size(); item.domain = "switch"; item.state = "off"; item.online = item.known = true; items.push_back(item);
        }
        rebuildView();
    } else {
        busy = true;
        if (xTaskCreate(worker, "home-assistant", 16384, nullptr, 1, nullptr) != pdPASS) {
            notice = "Network task failed - restart"; busy = false;
        }
    }
    draw();
    Serial.println(touchAvailable ? "TOUCH_PRESENT" : "NO_TOUCH: use BOOT select/hold");
    Serial.println(demo ? "SETUP_READY" : "CONFIGURED");
}

void loop() {
    static String line;
    static bool overflow = false;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            if (overflow) Serial.println("SETUP_ERROR line too long");
            else if (!line.isEmpty()) provision(line);
            line = ""; overflow = false;
        } else if (c != '\r' && !overflow) {
            if (line.length() < setupLineLimit) line += c; else overflow = true;
        }
    }
    static bool touched = false;
    static int touchId = -1;
    static unsigned touchRevision = 0;
    static unsigned long touchStarted = 0;
    if (touchAvailable) {
        Wire.beginTransmission(0x38); Wire.write(2);
        if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x38, 5) == 5) {
            const uint8_t count = Wire.read() & 15;
            const int xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
            const bool down = count > 0;
            if (down) {
                const bool waking = displayActivity();
                const int x = ((xh & 15) << 8) | xl, y = ((yh & 15) << 8) | yl;
                xSemaphoreTake(stateLock, portMAX_DELAY);
                const int id = UI::hit(x, y, viewItems.size(), page, cameraView());
                if (!touched) {
                    touchId = !waking && layoutDrawn && count == 1 ? id : -1;
                    touchStarted = millis();
                    touchRevision = layoutRevision;
                } else if (id != touchId || count != 1) {
                    touchId = -1;
                }
                xSemaphoreGive(stateLock);
            } else if (touched && touchId != -1) {
                xSemaphoreTake(stateLock, portMAX_DELAY);
                if (touchRevision == layoutRevision) activate(touchId, millis() - touchStarted >= holdMs);
                xSemaphoreGive(stateLock);
            }
            touched = down;
        }
    }
    static unsigned long pressedAt = 0;
    static bool pressed = false, buttonWakeOnly = false;
    bool down = digitalRead(0) == LOW;
    if (down) {
        bool waking = displayActivity();
        if (!pressed) { pressed = true; pressedAt = millis(); buttonWakeOnly = waking; }
    }
    if (!down && pressed) {
        unsigned long held = millis() - pressedAt; pressed = false;
        if (buttonWakeOnly) { buttonWakeOnly = false; }
        else if (held >= 800) {
            xSemaphoreTake(stateLock, portMAX_DELAY);
            int id = UI::focusedControl(focus, viewItems.size(), page, cameraView());
            activate(id, held >= 1800);
            xSemaphoreGive(stateLock);
        }
        else if (held >= 40) { xSemaphoreTake(stateLock, portMAX_DELAY); focus = (focus + 1) % UI::focusCount(viewItems.size(), page, cameraView()); dirty = true; xSemaphoreGive(stateLock); }
    }
    if (displayIdle.sleepDue(millis())) sleepDisplay();
    static time_t lastSecond = -1;
    time_t second = time(nullptr);
    static unsigned long lastFrame = 0;
    bool redraw;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    if (second != lastSecond) { lastSecond = second; dirty = true; }
    redraw = dirty || (scrollingNames && millis() - lastFrame >= 85); xSemaphoreGive(stateLock);
    if (redraw && !displayIdle.isAsleep()) {
        draw();
        lastFrame = millis();
    }
    delay(20);
}
