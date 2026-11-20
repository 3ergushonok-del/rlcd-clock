#include "services.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"

namespace {

WebServer         http(80);
services::Handlers hooks{};

const ui::State* curState     = nullptr;
time_t           curWeatherAt = 0;
uint32_t         curNextWake  = 0;
uint32_t         curAwakeLeft = 0;

bool otaActive = false;

// --- JSON -----------------------------------------------------------------

const char* wmoName(int code) {
    if (code < 0)                  return "unknown";
    if (code == 0)                 return "clear";
    if (code == 1)                 return "mostly_clear";
    if (code == 2)                 return "partly_cloudy";
    if (code == 3)                 return "cloudy";
    if (code == 45 || code == 48)  return "fog";
    if (code >= 51 && code <= 57)  return "drizzle";
    if (code >= 61 && code <= 67)  return "rain";
    if (code >= 71 && code <= 77)  return "snow";
    if (code >= 80 && code <= 82)  return "showers";
    if (code == 85 || code == 86)  return "snow_showers";
    if (code >= 95)                return "thunderstorm";
    return "cloudy";
}

String statusJson() {
    String j = "{";
    j += "\"device\":\"" MDNS_HOSTNAME "\",";
    j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    j += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    j += "\"uptime_s\":" + String(millis() / 1000) + ",";
    j += "\"next_wake_s\":" + String(curNextWake) + ",";
    j += "\"awake_left_s\":" + String(curAwakeLeft) + ",";
    j += "\"sleep_enabled\":";
    j += (hooks.sleepEnabled && hooks.sleepEnabled()) ? "true" : "false";
    j += ",";
    j += "\"weather_at\":" + String((long)curWeatherAt) + ",";

    j += "\"weather\":{";
    if (curState && curState->weather.valid) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "\"valid\":true,\"temp\":%.1f,\"feels\":%.1f,\"wind\":%.1f,"
                 "\"humidity\":%d,\"pressure\":%.0f,\"code\":%d,\"state\":\"%s\","
                 "\"tmin\":%.1f,\"tmax\":%.1f",
                 (double)curState->weather.temp, (double)curState->weather.feels,
                 (double)curState->weather.wind, curState->weather.humidity,
                 (double)curState->weather.pressure, curState->weather.code,
                 wmoName(curState->weather.code),
                 (double)curState->weather.tmin, (double)curState->weather.tmax);
        j += buf;
    } else {
        j += "\"valid\":false";
    }
    j += "},";

    j += "\"indoor\":{";
    if (curState && curState->indoorValid) {
        char buf[80];
        snprintf(buf, sizeof(buf), "\"valid\":true,\"temp\":%.1f,\"rh\":%.0f",
                 (double)curState->inTemp, (double)curState->inRh);
        j += buf;
    } else {
        j += "\"valid\":false";
    }
    j += "},";

    j += "\"battery\":{";
    if (curState && curState->batValid) {
        char buf[80];
        snprintf(buf, sizeof(buf), "\"valid\":true,\"volt\":%.2f,\"pct\":%d",
                 (double)curState->batVolt, curState->batPct);
        j += buf;
    } else {
        j += "\"valid\":false";
    }
    j += "}}";
    return j;
}

String forecastJson() {
    String j = "{\"days\":[";
    for (int i = 0; i < 3; ++i) {
        if (i) j += ",";
        j += "{";
        if (curState && curState->days[i].valid) {
            const net::Day& d = curState->days[i];
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "\"valid\":true,\"wday\":%d,\"day\":%d,\"month\":%d,"
                     "\"tmin\":%.1f,\"tmax\":%.1f,\"code\":%d,\"state\":\"%s\"",
                     d.wday, d.mday, d.mon + 1, (double)d.tmin, (double)d.tmax,
                     d.code, wmoName(d.code));
            j += buf;
        } else {
            j += "\"valid\":false";
        }
        j += "}";
    }
    j += "]}";
    return j;
}

// --- обработчики ----------------------------------------------------------

void touch() {
    if (hooks.extendAwake) hooks.extendAwake();
}

void handleStatus() {
    touch();
    http.sendHeader("Cache-Control", "no-store");
    http.send(200, "application/json", statusJson());
}

void handleForecast() {
    touch();
    http.sendHeader("Cache-Control", "no-store");
    http.send(200, "application/json", forecastJson());
}

void handleRefresh() {
    touch();
    if (hooks.requestRefresh) hooks.requestRefresh();
    http.send(200, "application/json", "{\"ok\":true}");
}

// /sleep?on=0 — держать плату активной (перед OTA или обслуживанием)
// /sleep?on=1 — вернуть обычный режим
void handleSleep() {
    touch();
    if (!http.hasArg("on")) {
        http.send(400, "application/json", "{\"error\":\"need ?on=0|1\"}");
        return;
    }
    const bool enabled = http.arg("on").toInt() != 0;
    if (hooks.setSleep) hooks.setSleep(enabled);
    String j = "{\"sleep_enabled\":";
    j += enabled ? "true}" : "false}";
    http.send(200, "application/json", j);
}

void handleNotFound() {
    touch();
    http.send(404, "application/json", "{\"error\":\"not found\"}");
}

// /test?partial=1 — принудительно перерисовать все зоны полосами.
// Нужен, чтобы проверять частичное обновление, не подключая USB.
void handleTest() {
    touch();
    if (http.arg("partial").toInt() != 0 && hooks.partialTest) hooks.partialTest();
    http.send(200, "application/json", "{\"ok\":true}");
}

void startOta() {
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
#ifdef OTA_PASSWORD
    if (OTA_PASSWORD[0] != '\0') ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
    ArduinoOTA.onStart([]() {
        otaActive = true;
        Serial.println("[ota] начало обновления");
    });
    ArduinoOTA.onEnd([]() {
        otaActive = false;
        Serial.println("[ota] готово");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        otaActive = false;
        Serial.printf("[ota] ошибка %u\n", (unsigned)e);
    });
    ArduinoOTA.begin();
    Serial.printf("[ota] включено, хост %s.local\n", MDNS_HOSTNAME);
}

}  // namespace

namespace services {

void begin(const Handlers& h) {
    hooks = h;

    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mdns] %s.local\n", MDNS_HOSTNAME);
    } else {
        Serial.println("[mdns] не запустился");
    }

    http.on("/status",   HTTP_GET,  handleStatus);
    http.on("/forecast", HTTP_GET,  handleForecast);
    http.on("/refresh",  HTTP_POST, handleRefresh);
    http.on("/sleep",    HTTP_POST, handleSleep);
    http.on("/test",     HTTP_POST, handleTest);
    http.onNotFound(handleNotFound);
    http.begin();
    Serial.println("[http] сервер на порту 80: /status /forecast /refresh /sleep");

    startOta();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) return;
    http.handleClient();
    ArduinoOTA.handle();
}

void publish(const ui::State* state, time_t weatherAt, uint32_t nextWakeS,
             uint32_t awakeLeftS) {
    curState     = state;
    curWeatherAt = weatherAt;
    curNextWake  = nextWakeS;
    curAwakeLeft = awakeLeftS;
}

bool holdOn() { return otaActive; }

bool otaRunning() { return otaActive; }

}  // namespace services