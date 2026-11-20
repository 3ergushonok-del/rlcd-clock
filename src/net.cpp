#include "net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <math.h>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Нет include/secrets.h — скопируй include/secrets.example.h и заполни"
#endif

namespace {

const char* const NTP1 = "pool.ntp.org";
const char* const NTP2 = "time.cloudflare.com";
const char* const NTP3 = "ntp1.vniiftri.ru";

// Погода: met.no (Норвежский метеорологический институт), без API-ключа.
// Open-Meteo у части провайдеров РФ недоступен (TCP-таймаут), поэтому не используется.
String weatherUrl() {
    String url = "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=";
    url += CITY_LAT;
    url += "&lon=";
    url += CITY_LON;
    return url;
}

// symbol_code из met.no -> WMO-код, чтобы переиспользовать тексты и иконки
int symbolToWmo(const char* s) {
    if (!strncmp(s, "clearsky", 8))      return 0;
    if (!strncmp(s, "fair", 4))          return 1;
    if (!strncmp(s, "partlycloudy", 12)) return 2;
    if (!strncmp(s, "cloudy", 6))        return 3;
    if (!strncmp(s, "fog", 3))           return 45;
    if (strstr(s, "thunder"))            return 95;
    if (strstr(s, "sleet"))              return 66;
    if (strstr(s, "snow"))               return 73;
    if (strstr(s, "heavyrain"))          return 65;
    if (strstr(s, "rain"))               return 61;
    return 3;
}

// Дни с 1970-01-01 (алгоритм Говарда Хиннанта), без зависимости от TZ.
long daysFromCivil(int y, int m, int d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy =
        (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097L + (long)doe - 719468L;
}

const char* findKey(const char* from, const char* key) {
    if (!from) return nullptr;
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(from, pat);
    if (!p) return nullptr;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '[') ++p;
    return p;
}

bool numFrom(const char* from, const char* key, float* out) {
    const char* p = findKey(from, key);
    if (!p) return false;
    char* end = nullptr;
    const double v = strtod(p, &end);
    if (end == p) return false;
    *out = (float)v;
    return true;
}

}  // namespace

namespace net {

void begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

bool wifiConnect(uint32_t timeoutMs) {
    if (strlen(WIFI_SSID) == 0) return false;
    if (wifiConnected()) return true;

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const uint32_t t0 = millis();
    while (!wifiConnected() && millis() - t0 < timeoutMs) {
        delay(200);
    }
    return wifiConnected();
}

int8_t rssi() { return wifiConnected() ? (int8_t)WiFi.RSSI() : 0; }

void ipString(char* buf, size_t n) {
    if (!wifiConnected()) { snprintf(buf, n, "-"); return; }
    snprintf(buf, n, "%s", WiFi.localIP().toString().c_str());
}

bool timeSync(uint32_t timeoutMs) {
    configTime(0, 0, NTP1, NTP2, NTP3);

    const uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        // Важно: проверяем именно статус SNTP, а не «время похоже на правду»,
        // иначе время, взятое из RTC, будет принято за синхронизированное.
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED &&
            time(nullptr) > 1700000000) {
            return true;
        }
        delay(200);
    }
    return false;
}

bool weatherFetch(Weather* now, Day* days, int dayCount) {
    if (!wifiConnected()) return false;

    WiFiClientSecure client;
    client.setInsecure();                 // публичные данные без аутентификации
    client.setTimeout(15);

    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    http.setReuse(false);
    // met.no требует осмысленный User-Agent
    http.setUserAgent("rlcd-clock/1.0 (personal weather display)");

    if (!http.begin(client, weatherUrl())) return false;
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[weather] http %d\n", code);
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();

    const char* json = payload.c_str();
    const char* ts = strstr(json, "\"timeseries\"");
    if (!ts) return false;

    Weather w;
    float v = 0;
    if (!numFrom(ts, "air_temperature", &v)) return false;
    w.valid = true;
    w.temp = v;

    if (numFrom(ts, "relative_humidity", &v)) w.humidity = (int)lroundf(v);
    if (numFrom(ts, "wind_speed", &v))        w.wind = v;
    if (numFrom(ts, "air_pressure_at_sea_level", &v)) w.pressure = v;

    // символ для текущей погоды — из первой записи
    const char* symNow = strstr(ts, "\"symbol_code\":\"");
    if (symNow) {
        symNow += strlen("\"symbol_code\":\"");
        char name[40];
        size_t i = 0;
        while (*symNow && *symNow != '"' && i < sizeof(name) - 1) name[i++] = *symNow++;
        name[i] = '\0';
        w.code = symbolToWmo(name);
    }

    // В compact-формате met.no нет «ощущается»: считаем wind chill для холода.
    w.feels = w.temp;
    if (w.temp <= 10.0f && w.wind >= 1.3f) {
        const float p = powf(w.wind * 3.6f, 0.16f);
        w.feels = 13.12f + 0.6215f * w.temp - 11.37f * p + 0.3965f * w.temp * p;
    }

    // --- прогноз по суткам ---
    for (int i = 0; i < dayCount; ++i) {
        days[i] = Day{};
        days[i].tmin = 1000.0f;
        days[i].tmax = -1000.0f;
    }
    int noonDist[8];
    for (int i = 0; i < dayCount && i < 8; ++i) noonDist[i] = 999;

    const long day0 = (long)((time(nullptr) + TZ_OFFSET_SEC) / 86400);

    const char* p = ts;
    while (true) {
        const char* t = strstr(p, "\"time\":\"");
        if (!t) break;
        const char* nextTime = strstr(t + 8, "\"time\":\"");

        int Y = 0, M = 0, D = 0, hh = 0, mm = 0;
        if (sscanf(t + 8, "%4d-%2d-%2dT%2d:%2d", &Y, &M, &D, &hh, &mm) == 5) {
            const long localEpoch =
                daysFromCivil(Y, M, D) * 86400L + hh * 3600L + mm * 60L + TZ_OFFSET_SEC;
            const int slot = (int)(localEpoch / 86400L - day0);
            const int hour = (int)((localEpoch % 86400L) / 3600L);

            if (slot >= 0 && slot < dayCount && slot < 8) {
                float at = 0;
                if (numFrom(t, "air_temperature", &at)) {
                    if (at < days[slot].tmin) days[slot].tmin = at;
                    if (at > days[slot].tmax) days[slot].tmax = at;
                }
                // символ берём из записи, ближайшей к полудню
                const char* sym = strstr(t, "\"symbol_code\":\"");
                if (sym && (!nextTime || sym < nextTime)) {
                    sym += strlen("\"symbol_code\":\"");
                    char nm[40];
                    size_t k = 0;
                    while (*sym && *sym != '"' && k < sizeof(nm) - 1) nm[k++] = *sym++;
                    nm[k] = '\0';
                    const int dist = abs(hour - 12);
                    if (dist < noonDist[slot]) {
                        noonDist[slot] = dist;
                        days[slot].code = symbolToWmo(nm);
                    }
                }
            }
        }
        if (!nextTime) break;
        p = nextTime;
    }

    for (int i = 0; i < dayCount; ++i) {
        if (days[i].tmax > -900.0f) {
            days[i].valid = true;
            const time_t de = (time_t)((day0 + i) * 86400L + 43200L);
            struct tm dt = {};
            gmtime_r(&de, &dt);
            days[i].wday = dt.tm_wday;
            days[i].mday = dt.tm_mday;
            days[i].mon  = dt.tm_mon;
        }
    }

    w.tmax = days[0].valid ? days[0].tmax : w.temp;
    w.tmin = days[0].valid ? days[0].tmin : w.temp;

    *now = w;
    return true;
}

}  // namespace net