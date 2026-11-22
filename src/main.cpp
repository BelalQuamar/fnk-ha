#include <Arduino.h>
#include <TFT_eSPI.h>
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
SemaphoreHandle_t stateLock;
QueueHandle_t actions;
std::vector<PowerItem> items;
String notice = "Starting...";
bool demo = true, busy = false, touchAvailable = false, dirty = true;
int focus = 0;
int page = 0;
unsigned renderCount = 0;
unsigned discoveredCount = 0;
bool catalogLive = false, layoutDrawn = false;
const uint16_t BG = 0x2108, CARD = 0x4208, MUTED = 0x9CF3, GREEN = 0x052C;
DisplayIdle displayIdle;
constexpr size_t setupLineLimit = 3500;
void draw();

void sleepDisplay() {
    if (displayIdle.isAsleep()) return;
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
        screen.writecommand(ILI9341_SLPOUT);
        delay(120);
        screen.writecommand(TFT_DISPON);
        draw();
        digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    }
    return waking;
}

struct Action { int index; bool on; }; // -1 refresh, -2 all

void status(const String& text, bool working) {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    notice = text; busy = working; dirty = true;
    xSemaphoreGive(stateLock);
    Serial.println(text); // Never log credentials, tokens, or full API responses.
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
    DynamicJsonDocument json(4096);
    if (!deserializeJson(json, prefs.getString("config"))) {
        settings = readSettings(json.as<JsonObjectConst>());
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
        json["clock_synced"] = now >= 1700000000;
        tm local; localtime_r(&now, &local);
        char localText[48]; strftime(localText, sizeof(localText), "%Y-%m-%d %H:%M:%S %Z", &local);
        json["local_time"] = localText; json["timezone"] = "America/Toronto";
        xSemaphoreTake(stateLock, portMAX_DELAY);
        json["channels"] = items.size(); json["status"] = notice;
        json["ui_version"] = 6; json["font2_loaded"] = true;
        json["devices_per_page"] = UI::devicesPerPage;
        json["page"] = page+1; json["pages"] = UI::pageCount(items.size());
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
    unsigned long lastDiscovery = millis();
    for (;;) {
        const bool connected = WiFi.status() == WL_CONNECTED;
        const bool reconnected = connected && !wasConnected;
        wasConnected = connected;
        if (connected && !clockStarted) {
            configTzTime(localTimezone, ntpPrimary, ntpSecondary);
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
            if (!automatic) continue;
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
            if (ha.discover(found)) {
                const unsigned total = found.size();
                xSemaphoreTake(stateLock, portMAX_DELAY);
                if (items.size() != found.size() ||
                    !std::equal(items.begin(), items.end(), found.begin(), [](const PowerItem& a, const PowerItem& b) {
                        return a.id == b.id && a.name == b.name && a.state == b.state;
                    })) layoutDrawn = false;
                items.swap(found);
                catalogLive = true;
                discoveredCount = total;
                page = UI::clampPage(page, items.size());
                if (focus >= UI::focusCount(items.size(),page)) focus = 0;
                xSemaphoreGive(stateLock);
                status(String(total) + " entities / auto refresh 60s", false);
            } else {
                xSemaphoreTake(stateLock, portMAX_DELAY);
                catalogLive = false;
                xSemaphoreGive(stateLock);
                status(ha.error, false);
            }
            lastDiscovery = millis();
            continue;
        }
        status("Sending command...", true);
        int sent = 0, failed = 0, skipped = 0, unconfirmed = 0;
        String lastError;
        xSemaphoreTake(stateLock, portMAX_DELAY);
        const size_t count = items.size();
        xSemaphoreGive(stateLock);
        for (size_t i = 0; i < count; ++i) {
            if (action.index >= 0 && (int)i != action.index) continue;
            xSemaphoreTake(stateLock, portMAX_DELAY);
            PowerItem item = items[i];
            xSemaphoreGive(stateLock);
            if (!item.online || !item.controllable()) { ++skipped; continue; }
            xSemaphoreTake(stateLock, portMAX_DELAY);
            items[i].sent = true; dirty = true;
            xSemaphoreGive(stateLock);
            if (ha.command(item, action.on)) {
                ++sent;
                // Confirm the state reported by Home Assistant after the command.
                bool confirmed = false;
                for (int attempt = 0; attempt < 3; ++attempt) {
                    vTaskDelay(pdMS_TO_TICKS(600));
                    if (ha.refreshState(item) && item.known && item.online && item.on == action.on) {
                        confirmed = true; break;
                    }
                }
                if (!confirmed) { ++unconfirmed; item.known = false; }
                item.sent = false;
                xSemaphoreTake(stateLock, portMAX_DELAY);
                items[i] = item; dirty = true;
                xSemaphoreGive(stateLock);
            } else {
                ++failed; lastError = ha.error;
                xSemaphoreTake(stateLock, portMAX_DELAY);
                items[i].sent = false; dirty = true;
                items[i].known = false;
                xSemaphoreGive(stateLock);
            }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        String result;
        if (unconfirmed) result = String(unconfirmed) + " unconfirmed - tap Refresh";
        else if (failed) result = String(failed) + " failed - tap Refresh";
        else if (action.index >= 0 && sent) result = action.on ? "Device is ON" : "Device is OFF";
        else result = String(sent) + " changed / " + skipped + " skipped";
        status(result, false);
        if (failed) { Serial.println(lastError); }
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
void button(int id, const char* text, int x, int y, int w, int h, uint16_t color, bool enabled = true) {
    canvas.fillRoundRect(x, y, w, h, 8, enabled ? color : CARD);
    if (!touchAvailable && id == UI::focusedControl(focus, items.size(), page)) canvas.drawRoundRect(x, y, w, h, 8, TFT_WHITE);
    canvas.setTextDatum(MC_DATUM); canvas.setTextColor(enabled ? TFT_WHITE : MUTED);
    canvas.drawString(text, x + w/2, y + h/2, 2);
}

// Fit names by measured pixel width, wrapping at spaces before shortening.
void deviceName(String name, UI::Rect rect, uint16_t foreground, uint16_t background) {
    name.trim();
    if (name.isEmpty()) name = "Unnamed device";
    canvas.setTextDatum(MC_DATUM); canvas.setTextColor(foreground, background);
    int font = rect.w >= 200 && rect.h >= 65 ? 4 : rect.h >= 42 ? 2 : 1;
    int available = rect.w - 20;
    int cx = rect.x + rect.w/2;
    int centerY = rect.y + (rect.h-16)/2;
    if (canvas.textWidth(name, font) <= available) {
        canvas.drawString(name, cx, centerY, font); return;
    }
    unsigned cut = 0;
    while (cut < name.length() && canvas.textWidth(name.substring(0, cut+1), font) <= available) ++cut;
    int space = name.lastIndexOf(' ', cut);
    if (space > 0) cut = space;
    String first = name.substring(0, cut), second = name.substring(cut);
    second.trim();
    int height = canvas.fontHeight(font);
    if (rect.h < height*2 + 20) {
        while (first.length() && canvas.textWidth(first + "..", font) > available) first.remove(first.length()-1);
        canvas.drawString(first + "..", cx, centerY, font); return;
    }
    if (canvas.textWidth(second, font) > available) {
        while (second.length() && canvas.textWidth(second + "..", font) > available) second.remove(second.length()-1);
        second += "..";
    }
    canvas.drawString(first, cx, centerY - height/2, font);
    canvas.drawString(second, cx, centerY + height/2, font);
}

void draw() {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    dirty = false;
    ++renderCount;
    canvas.fillSprite(BG);
    const time_t now = time(nullptr);
    const bool synced = now >= 1700000000;
    tm local; localtime_r(&now, &local);
    char clockText[12] = "--:--", secondsText[4] = ":--", dateText[40] = "Waiting for NTP";
    char zoneText[12] = "Toronto";
    if (synced) {
        strftime(clockText, sizeof(clockText), "%H:%M", &local);
        strftime(secondsText, sizeof(secondsText), ":%S", &local);
        strftime(dateText, sizeof(dateText), "%d %b %Y", &local);
        strftime(zoneText, sizeof(zoneText), "%Z", &local);
    }
    canvas.setTextDatum(TL_DATUM); canvas.setTextColor(TFT_WHITE, BG);
    canvas.setTextSize(2); canvas.drawString(clockText, 8, 0, 4); canvas.setTextSize(1);
    canvas.drawString(secondsText, 140, 30, 2);
    label(dateText, 8, 52, synced ? 4 : 2, TFT_WHITE);
    canvas.setTextDatum(TR_DATUM); canvas.setTextColor(MUTED, BG);
    canvas.drawString("FNK_HA", 232, 4, 1);
    canvas.drawString(zoneText, 232, 16, 1);
    page = UI::clampPage(page, items.size());
    const int pages = UI::pageCount(items.size());
    const int visible = UI::visibleCount(items.size(),page);
    if (pages > 1) {
        button(UI::previous, "<", 166, 28, 28, 28, CARD, page > 0);
        button(UI::next, ">", 204, 28, 28, 28, CARD, page+1 < pages);
        canvas.setTextDatum(MC_DATUM); canvas.setTextColor(MUTED, BG);
        canvas.drawString(String(page+1) + "/" + pages, 199, 65, 1);
    } else {
        canvas.setTextDatum(TR_DATUM); canvas.setTextColor(MUTED, BG);
        canvas.drawString(String(items.size()), 230, 38, 2);
    }
    label(shortText(demo ? "DEMO - configure via USB" : notice, 36), 8, 80, 1, TFT_CYAN);
    for (int slot = 0; slot < visible; ++slot) {
        size_t index = page*UI::devicesPerPage + slot;
        UI::Rect rect = UI::tile(slot, visible);
        const auto& item = items[index];
        const bool online = item.online && (catalogLive || demo);
        const bool pending = item.controllable() && (item.sent || !item.known);
        uint16_t fill = !online ? 0x632C : pending ? 0xFBC0 :
                        item.known && item.on ? GREEN : item.known ? TFT_BLACK : CARD;
        uint16_t ink = online && pending ? 0x18C3 : TFT_WHITE;
        int radius = min(12, min(rect.h/3, rect.w/3));
        canvas.fillRoundRect(rect.x, rect.y, rect.w, rect.h, radius, fill);
        canvas.drawRoundRect(rect.x, rect.y, rect.w, rect.h, radius,
            !touchAvailable && (int)index == UI::focusedControl(focus, items.size(), page) ? TFT_WHITE : item.on && item.known ? 0x4F77 : 0x528A);
        deviceName(item.name, rect, ink, fill);
        String state = !online ? "OFFLINE" : item.sent ? "UPDATING" :
            item.controllable() && !item.known ? "UNKNOWN" :
            item.known ? (item.on ? "ON" : "OFF") : item.state;
        const int width = rect.w - 12;
        if (canvas.textWidth(state, 1) > width) {
            while (!state.isEmpty() && canvas.textWidth(state + "..", 1) > width) state.remove(state.length() - 1);
            state += "..";
        }
        canvas.setTextDatum(MC_DATUM); canvas.setTextColor(ink, fill);
        if (rect.h >= 35) canvas.drawString(state, rect.x + rect.w/2, rect.y + rect.h - 12, 1);
    }
    if (items.empty()) {
        label(busy ? "Loading devices..." : "No devices loaded", 20, 110, 2, MUTED);
        label("Tap Refresh to try again", 20, 140, 2, MUTED);
    }
    const bool canControl = !busy && (catalogLive || demo) &&
        std::any_of(items.begin(), items.end(), [](const PowerItem& item) {
            return item.online && item.controllable();
        });
    button(UI::allOn, "ALL ON", 8, 284, 70, 32, GREEN, canControl);
    button(UI::allOff, "ALL OFF", 85, 284, 70, 32, TFT_BLACK, canControl);
    button(UI::refresh, busy ? "WAIT" : "REFRESH", 162, 284, 70, 32, 0x2250, !busy);
    canvas.pushSprite(0, 0);
    layoutDrawn = true;
    xSemaphoreGive(stateLock);
}

// Called with stateLock held.
void activate(int id) {
    if (id == UI::previous || id == UI::next) {
        page = UI::clampPage(page + (id == UI::next ? 1 : -1), items.size());
        focus = 0; dirty = true; layoutDrawn = false;
        return;
    }
    if (busy || (!layoutDrawn && id != UI::refresh) ||
        (!demo && !catalogLive && id != UI::refresh)) { return; }
    Action action{-1, false};
    if (id >= 0 && id < (int)items.size()) {
        action.index = id;
        if ((size_t)action.index >= items.size() || !items[action.index].online || !items[action.index].controllable()) {
            return;
        }
        // Read unknown states instead of guessing which command a toggle means.
        if (!items[action.index].known) action.index = -1;
        else action.on = !items[action.index].on;
    } else if (id == UI::allOn || id == UI::allOff) {
        action.index = -2; action.on = id == UI::allOn;
    } else if (id != UI::refresh) { return; }
    if (demo) {
        if (action.index != -1) for (size_t i = 0; i < items.size(); ++i)
            if (action.index == -2 || action.index == (int)i) { items[i].on = action.on; items[i].state = action.on ? "on" : "off"; }
        notice = "Demo only - configure via USB"; dirty = true;
    } else if (xQueueSend(actions, &action, 0) == pdTRUE) {
        busy = true; notice = "Updating device..."; dirty = true;
    }
}

void setup() {
    // Native USB CDC defaults to 256 bytes, smaller than a setup message.
    Serial.setRxBufferSize(setupLineLimit + 2);
    Serial.begin(115200);
    stateLock = xSemaphoreCreateMutex(); actions = xQueueCreate(1, sizeof(Action));
    setenv("TZ", localTimezone, 1); tzset();
    screen.init(); screen.setRotation(0);
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    displayIdle.activity(millis());
    canvas.setColorDepth(8);
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
            PowerItem item; item.name = name; item.domain = "switch"; item.state = "off"; item.online = item.known = true; items.push_back(item);
        }
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
    if (touchAvailable) {
        Wire.beginTransmission(0x38); Wire.write(2);
        if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x38, 5) == 5) {
            const uint8_t count = Wire.read() & 15;
            const int xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
            bool wakeGesture = false;
            if (count > 0 && count <= 2) wakeGesture = displayActivity();
            if (count > 0 && count <= 2 && !touched && !wakeGesture) {
                const int x = ((xh & 15) << 8) | xl, y = ((yh & 15) << 8) | yl;
                if (x < 240 && y < 320) { xSemaphoreTake(stateLock, portMAX_DELAY);
                    int id = UI::hit(x, y, items.size(), page);
                    if (id != -1) activate(id);
                    xSemaphoreGive(stateLock); }
            }
            touched = count > 0;
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
            int id = UI::focusedControl(focus, items.size(), page);
            activate(id);
            xSemaphoreGive(stateLock);
        }
        else if (held >= 40) { xSemaphoreTake(stateLock, portMAX_DELAY); focus = (focus + 1) % UI::focusCount(items.size(),page); dirty = true; xSemaphoreGive(stateLock); }
    }
    if (displayIdle.sleepDue(millis())) sleepDisplay();
    static time_t lastSecond = -1;
    time_t second = time(nullptr);
    bool redraw;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    if (second != lastSecond) { lastSecond = second; dirty = true; }
    redraw = dirty; xSemaphoreGive(stateLock);
    if (redraw && !displayIdle.isAsleep()) draw();
    delay(20);
}
