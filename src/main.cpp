// Погода и дата для Waveshare ESP32-S3-RLCD-4.2
//
// Работает в режиме «проснулся — обновился — уснул»:
//   * таймер просыпания раз в SLEEP_INTERVAL_S (3 часа)
//   * кнопки BOOT/KEY будят плату и дают 2 минуты на взаимодействие
//
// Время: SNTP + PCF85063. Погода: met.no. Датчики: SHTC3, ADC батареи.
// Управление:
//   KEY  короткое — экран погоды ↔ прогноз на 3 дня
//   KEY  долгое   — обновить погоду сейчас
//   BOOT короткое — экран состояния
//   BOOT долгое   — инверсия экрана

#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "gfx.h"
#include "hw.h"
#include "net.h"
#include "panel.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Нет include/secrets.h — скопируй include/secrets.example.h и заполни"
#endif
#include "services.h"
#include "ui.h"

namespace {

ui::State state;
Preferences prefs;

bool     invert       = false;
bool     showStatus   = false;
uint32_t statusUntil  = 0;
bool     dirty        = true;

enum Screen { SCREEN_MAIN, SCREEN_FORECAST };
Screen   screen        = SCREEN_MAIN;
uint32_t forecastUntil = 0;
time_t   weatherAt     = 0;

uint32_t bootMs       = 0;
uint32_t lastWeather  = 0;
uint32_t lastSensor   = 0;
uint32_t lastNtp      = 0;
uint32_t lastStatusUi = 0;
uint32_t lastWifiTry  = 0;

uint32_t awakeUntil   = 0;
bool     stayAwake    = false;
bool     wakeByButton = false;

bool     sleepEnabled   = true;   // управляется сервером через POST /sleep
bool     refreshNow     = false;  // запрос обновления погоды извне
bool     servicesUp     = false;

bool     needFullFrame  = true;   // следующий кадр рисовать целиком
uint16_t forceMask      = 0;      // принудительная перерисовка зон (отладка)

char ipBuf[20] = "-";

// --- кэш между пробуждениями ---------------------------------------------
// Простые POD-структуры: их можно держать в RTC-памяти, которая не теряется
// при глубоком сне и не требует инициализации при загрузке.

constexpr uint32_t PERSIST_MAGIC = 0xC10C4B12;

struct PersistWeather {
    bool  valid;
    float temp, feels, wind, tmin, tmax, pressure;
    int   humidity;
    int   code;
};

struct PersistDay {
    bool  valid;
    int   wday, mday, mon;
    float tmax, tmin;
    int   code;
};

struct Persist {
    uint32_t       magic;
    int32_t        weatherAt;
    PersistWeather w;
    PersistDay     d[3];
};

RTC_DATA_ATTR Persist persist;

void savePersist() {
    persist.w.valid    = state.weather.valid;
    persist.w.temp     = state.weather.temp;
    persist.w.feels    = state.weather.feels;
    persist.w.wind     = state.weather.wind;
    persist.w.tmin     = state.weather.tmin;
    persist.w.tmax     = state.weather.tmax;
    persist.w.pressure = state.weather.pressure;
    persist.w.humidity = state.weather.humidity;
    persist.w.code     = state.weather.code;

    for (int i = 0; i < 3; ++i) {
        persist.d[i].valid = state.days[i].valid;
        persist.d[i].wday  = state.days[i].wday;
        persist.d[i].mday  = state.days[i].mday;
        persist.d[i].mon   = state.days[i].mon;
        persist.d[i].tmax  = state.days[i].tmax;
        persist.d[i].tmin  = state.days[i].tmin;
        persist.d[i].code  = state.days[i].code;
    }

    persist.weatherAt = (int32_t)weatherAt;
    persist.magic     = PERSIST_MAGIC;
}

void loadPersist() {
    if (persist.magic != PERSIST_MAGIC) return;

    state.weather.valid    = persist.w.valid;
    state.weather.temp     = persist.w.temp;
    state.weather.feels    = persist.w.feels;
    state.weather.wind     = persist.w.wind;
    state.weather.tmin     = persist.w.tmin;
    state.weather.tmax     = persist.w.tmax;
    state.weather.pressure = persist.w.pressure;
    state.weather.humidity = persist.w.humidity;
    state.weather.code     = persist.w.code;

    for (int i = 0; i < 3; ++i) {
        state.days[i].valid = persist.d[i].valid;
        state.days[i].wday  = persist.d[i].wday;
        state.days[i].mday  = persist.d[i].mday;
        state.days[i].mon   = persist.d[i].mon;
        state.days[i].tmax  = persist.d[i].tmax;
        state.days[i].tmin  = persist.d[i].tmin;
        state.days[i].code  = persist.d[i].code;
    }

    weatherAt = (time_t)persist.weatherAt;
}

// --- кнопки ---------------------------------------------------------------

struct BtnState {
    bool     pressed   = false;
    bool     longSent  = false;
    uint32_t downMs    = 0;
    uint32_t changedMs = 0;
};

BtnState btnBoot, btnKey;

// 0 — нет события, 1 — клик, 2 — долгое нажатие
int pollButton(BtnState& b, int pin) {
    const bool now = digitalRead(pin) == LOW;
    if (now != b.pressed) {
        if (millis() - b.changedMs < 25) return 0;   // антидребезг
        b.changedMs = millis();
        b.pressed = now;
        if (now) {
            b.downMs = millis();
            b.longSent = false;
        } else if (!b.longSent) {
            return 1;
        }
    }
    if (b.pressed && !b.longSent && millis() - b.downMs > 900) {
        b.longSent = true;
        return 2;
    }
    return 0;
}

// --- время ----------------------------------------------------------------
// Локальное время = UTC + фиксированное смещение. Не полагаемся на TZ-строку:
// newlib в ESP-IDF несогласованно трактует POSIX TZ в mktime() и localtime().

void localTm(time_t t, struct tm* out) {
    const time_t shifted = t + TZ_OFFSET_SEC;
    gmtime_r(&shifted, out);
}

// Обратное преобразование: на входе уже локальные поля.
time_t epochFromLocalTm(const struct tm& t) {
    int y = t.tm_year + 1900;
    int m = t.tm_mon + 1;
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy =
        (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)t.tm_mday - 1u;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097L + (long)doe - 719468L;
    const long secs = days * 86400L + t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
    return (time_t)(secs - TZ_OFFSET_SEC);
}

void applyEpoch(time_t t) {
    timeval tv;
    tv.tv_sec = t;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

void logNow(const char* tag) {
    const time_t now = time(nullptr);
    struct tm t = {};
    localTm(now, &t);
    Serial.printf("[%s] %ld -> %04d-%02d-%02d %02d:%02d\n", tag, (long)now,
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
}

void saveRtcFromSystem() {
    const time_t now = time(nullptr);
    if (now < 1700000000) return;
    struct tm t = {};
    localTm(now, &t);
    hw::rtcWrite(t);
}

// --- отрисовка ------------------------------------------------------------

struct Range { int x0, x1; };

// Отправляет только изменившиеся зоны: сливаем их X-диапазоны в минимальное
// число полос и на каждую вызываем частичное обновление панели.
void flushRegions(uint16_t mask) {
    Range r[ui::regionCount()];
    int n = 0;
    for (int i = 0; i < ui::regionCount(); ++i) {
        if (!(mask & (1u << i))) continue;
        const ui::Box b = ui::regionBox(i);
        r[n].x0 = b.x;
        r[n].x1 = b.x + b.w - 1;
        ++n;
    }
    if (n == 0) return;

    for (int i = 1; i < n; ++i) {           // сортировка вставками
        const Range key = r[i];
        int j = i - 1;
        while (j >= 0 && r[j].x0 > key.x0) { r[j + 1] = r[j]; --j; }
        r[j + 1] = key;
    }

    int strips = 0;
    int bytes = 0;
    int i = 0;
    while (i < n) {
        int x0 = r[i].x0;
        int x1 = r[i].x1;
        int j = i + 1;
        while (j < n && r[j].x0 <= x1 + 1) {
            if (r[j].x1 > x1) x1 = r[j].x1;
            ++j;
        }
        panel::flushX(gfx::fb, x0, x1, invert);
        bytes += ((x1 >> 1) - (x0 >> 1) + 1) * 75;
        ++strips;
        i = j;
    }
    Serial.printf("[flush] полос %d, %d из 15000 байт\n", strips, bytes);
}

void draw() {
    if (showStatus || screen == SCREEN_FORECAST) {
        if (showStatus) {
            ui::renderStatus(state, ipBuf, (millis() - bootMs) / 1000);
        } else {
            char stamp[26] = "";
            if (weatherAt > 1700000000) {
                struct tm t = {};
                localTm(weatherAt, &t);
                snprintf(stamp, sizeof(stamp), "обновлено %02d:%02d", t.tm_hour, t.tm_min);
            }
            ui::renderForecast(state, stamp);
        }
        panel::flush(gfx::fb, invert);
        needFullFrame = true;   // при возврате на погоду кадр уже не тот
        dirty = false;
        return;
    }

    const bool full = needFullFrame;
    const uint16_t mask = ui::renderWeather(state, full, forceMask);
    forceMask = 0;
    needFullFrame = false;

    if (full) {
        panel::flush(gfx::fb, invert);
    } else if (mask) {
        flushRegions(mask);
    }

    dirty = false;
}

void drawBoot(const char* title, const char* subtitle) {
    ui::renderBoot(title, subtitle);
    panel::flush(gfx::fb, invert);
    needFullFrame = true;
}

// --- данные ---------------------------------------------------------------

void updateTime() {
    const time_t now = time(nullptr);
    const bool ok = now > 1700000000;

    struct tm t = {};
    localTm(now, &t);

    if (state.timeValid != ok) dirty = true;
    state.timeValid = ok;
    state.hour   = t.tm_hour;
    state.minute = t.tm_min;
    if (state.wday != t.tm_wday || state.mday != t.tm_mday || state.mon != t.tm_mon) dirty = true;
    state.wday = t.tm_wday;
    state.mday = t.tm_mday;
    state.mon  = t.tm_mon;
}

void readSensors() {
    float t = 0, rh = 0;
    if (hw::shtc3Read(&t, &rh)) {
        if (fabsf(t - state.inTemp) > 0.05f || fabsf(rh - state.inRh) > 0.5f) dirty = true;
        state.indoorValid = true;
        state.inTemp = t;
        state.inRh = rh;
    }
    const float v = hw::batteryVoltage();
    const uint8_t pct = hw::batteryLevel();
    if (pct != state.batPct || fabsf(v - state.batVolt) > 0.02f) dirty = true;
    state.batValid = true;
    state.batVolt = v;
    state.batPct = pct;
}

void refreshWeather() {
    if (!net::wifiConnected()) {
        state.wifiOk = net::wifiConnect(15000);
        net::ipString(ipBuf, sizeof(ipBuf));
    }
    state.wifiOk = net::wifiConnected();
    state.rssi = net::rssi();

    // Одна повторная попытка: первый TLS-хендшейк иногда отваливается по таймауту.
    for (int attempt = 0; attempt < 2; ++attempt) {
        net::Weather w;
        if (net::weatherFetch(&w, state.days, 3)) {
            state.weather = w;
            weatherAt = time(nullptr);
            savePersist();
            dirty = true;
            Serial.printf("[weather] %.1f C, code %d, wind %.1f, hum %d, %.0f hPa\n",
                          (double)w.temp, w.code, (double)w.wind, w.humidity,
                          (double)w.pressure);
            for (int i = 0; i < 3; ++i) {
                const net::Day& d = state.days[i];
                Serial.printf("[forecast] %02d.%02d: %s, %.1f..%.1f\n",
                              d.mday, d.mon + 1, d.valid ? "ok" : "no",
                              (double)d.tmin, (double)d.tmax);
            }
            lastWeather = millis();
            return;
        }
        Serial.printf("[weather] попытка %d не удалась\n", attempt + 1);
        delay(1500);
    }
    lastWeather = millis();
}

// --- сетевые сервисы ------------------------------------------------------
// Задел под внешний сервер: HTTP API, mDNS, OTA. Всё внутреннее живёт
// в services.cpp, здесь только связка с логикой сна и обновления.

void ensureServices() {
#if ENABLE_SERVICES
    if (servicesUp || !net::wifiConnected()) return;

    services::Handlers h;
    h.extendAwake = []() { awakeUntil = millis() + AWAKE_BUTTON_MS; };
    h.setSleep = [](bool on) {
        sleepEnabled = on;
        prefs.putBool("sleep", on);
        Serial.printf("[sleep] %s сервером\n", on ? "разрешён" : "отключён");
    };
    h.requestRefresh = []() { refreshNow = true; };
    h.sleepEnabled = []() { return sleepEnabled; };
    h.partialTest = []() {
        needFullFrame = false;
        forceMask = 0x3F;
        dirty = true;
        Serial.println("[test] частичная перерисовка всех зон (запрос по HTTP)");
    };

    services::begin(h);
    servicesUp = true;
#endif
}

void dumpFb() {
    Serial.println("SHOT");
    for (int i = 0; i < gfx::BYTES; ++i) {
        Serial.printf("%02x", gfx::fb[i]);
        if ((i % 60) == 59) Serial.println();
    }
    Serial.println();
    Serial.println("END");
}

// --- сон ------------------------------------------------------------------

// Просыпаемся по календарю: 00:05, 03:05, 06:05 и т.д., а не «через 3 часа
// от момента загрузки». Так дата меняется сразу после полуночи, а все
// обновления выровнены по сетке времени.
uint32_t secondsUntilNextWake() {
    const time_t now = time(nullptr);
    if (now < 1700000000) return SLEEP_INTERVAL_S;   // время неизвестно — просто интервал

    const long local  = (long)now + TZ_OFFSET_SEC;
    const long sod    = local % 86400;
    const long anchor = SLEEP_ANCHOR_MIN * 60L;

    long slot = anchor;
    while (slot <= sod) slot += SLEEP_INTERVAL_S;

    long delta = slot - sod;
    if (delta < 300) delta += SLEEP_INTERVAL_S;      // не просыпаться дважды подряд
    return (uint32_t)delta;
}

void goToSleep() {
    const uint32_t wait = secondsUntilNextWake();
    struct tm target = {};
    localTm(time(nullptr) + wait, &target);
    Serial.printf("[sleep] сон %lu с, до %02d:%02d\n",
                  (unsigned long)wait, target.tm_hour, target.tm_min);
    Serial.flush();
    delay(30);

    // Панель ST7305 держит изображение, только пока её не сбросили. В глубоком
    // сне GPIO «уплывают»: RST уходит в низ, панель перезагружается и картинка
    // пропадает. Поэтому прижимаем линии панели через GPIO hold.
    const int panelPins[] = {PIN_LCD_RST, PIN_LCD_CS, PIN_LCD_DC,
                             PIN_LCD_SCLK, PIN_LCD_MOSI};
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 1);   // вне сброса
    gpio_set_level((gpio_num_t)PIN_LCD_CS, 1);    // не выбрана
    gpio_set_level((gpio_num_t)PIN_LCD_DC, 0);
    gpio_set_level((gpio_num_t)PIN_LCD_SCLK, 0);  // без «фантомных» тактов
    gpio_set_level((gpio_num_t)PIN_LCD_MOSI, 0);
    for (int pin : panelPins) gpio_hold_en((gpio_num_t)pin);
    gpio_deep_sleep_hold_en();

    // Кнопки — активный низкий уровень. В глубоком сне внутренние подтяжки
    // включаются только через RTC-домен, иначе пины «плавают» и дают ложные пробуждения.
    const uint64_t mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_KEY);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_BOOT);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_KEY);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_BOOT);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_KEY);

    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)wait * 1000000ULL);
    esp_deep_sleep_start();
}

// Снимаем удержание линий после пробуждения, иначе SPI не заработает.
void releasePanelHold() {
    gpio_deep_sleep_hold_dis();
    const int panelPins[] = {PIN_LCD_RST, PIN_LCD_CS, PIN_LCD_DC,
                             PIN_LCD_SCLK, PIN_LCD_MOSI};
    for (int pin : panelPins) gpio_hold_dis((gpio_num_t)pin);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    wakeByButton = (cause == ESP_SLEEP_WAKEUP_EXT1);
    const bool coldBoot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

    Serial.printf("\n[clock] старт, пробуждение: %s\n",
                  coldBoot ? "включение" : (wakeByButton ? "кнопка" : "таймер"));

    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
    pinMode(PIN_BTN_KEY, INPUT_PULLUP);

    releasePanelHold();
    panel::init();
    gfx::clear();
    if (persist.magic != PERSIST_MAGIC) drawBoot("ПОГОДА", "инициализация...");

    prefs.begin("clock", false);
    invert = prefs.getBool("invert", false);
    sleepEnabled = prefs.getBool("sleep", true);

    net::begin();
    hw::begin();

    // Время: RTC — единственный источник сразу после пробуждения.
    struct tm rtc = {};
    uint8_t raw[7] = {0};
    time_t rtcEpoch = 0;
    if (hw::rtcRead(&rtc, raw)) {
        Serial.printf("[rtc] raw %02x %02x %02x %02x %02x %02x %02x\n",
                      raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6]);
        rtcEpoch = epochFromLocalTm(rtc);
        if (rtcEpoch < 1700000000) rtcEpoch = 0;
    }
    if (rtcEpoch > 0) applyEpoch(rtcEpoch);

    loadPersist();
    readSensors();
    updateTime();
    dirty = true;
    draw();          // показываем то, что уже знаем, без экранов загрузки

    const bool stale = (weatherAt == 0) ||
                       (time(nullptr) - (long)weatherAt > SLEEP_INTERVAL_S);
    const bool needNet = coldBoot || !wakeByButton || stale;

    if (needNet) {
        if (strlen(WIFI_SSID) == 0) {
            Serial.println("[wifi] SSID не задан (include/secrets.h)");
        } else {
            state.wifiOk = net::wifiConnect(20000);
            net::ipString(ipBuf, sizeof(ipBuf));
            Serial.printf("[wifi] %s, ip %s, rssi %d\n",
                          state.wifiOk ? "ok" : "fail", ipBuf, (int)net::rssi());

            if (state.wifiOk) {
                if (net::timeSync(15000)) {
                    state.timeSynced = true;
                    saveRtcFromSystem();
                    logNow("ntp");
                } else {
                    Serial.println("[ntp] не удалось");
                }
                refreshWeather();
            }
        }
        lastNtp = millis();
    } else {
        Serial.println("[net] данные свежие, сеть не нужна");
    }

    state.rssi = net::rssi();
    updateTime();
    readSensors();
    dirty = true;
    draw();

    ensureServices();

    bootMs = millis();
    lastSensor = millis();
    lastStatusUi = millis();

    awakeUntil = millis() + (coldBoot    ? AWAKE_COLD_MS
                             : wakeByButton ? AWAKE_BUTTON_MS
                                            : AWAKE_TIMER_MS);
    const uint32_t nextWake = secondsUntilNextWake();
    struct tm tgt = {};
    localTm(time(nullptr) + nextWake, &tgt);
    Serial.printf("[clock] готов, следующий подъём в %02d:%02d (через %lu с)\n",
                  tgt.tm_hour, tgt.tm_min, (unsigned long)nextWake);
}

void loop() {
    const uint32_t now = millis();

    const int keyEvent = pollButton(btnKey, PIN_BTN_KEY);
    if (keyEvent) awakeUntil = millis() + AWAKE_BUTTON_MS;
    switch (keyEvent) {
        case 1:   // переключение экрана
            screen = (screen == SCREEN_FORECAST) ? SCREEN_MAIN : SCREEN_FORECAST;
            forecastUntil = now + 120000;
            showStatus = false;
            Serial.printf("[key] экран: %s\n",
                          screen == SCREEN_FORECAST ? "прогноз" : "погода");
            dirty = true;
            break;
        case 2:   // обновить погоду немедленно
            Serial.println("[key] обновить погоду");
            drawBoot("ПОГОДА", "обновление...");
            refreshWeather();
            lastSensor = 0;
            dirty = true;
            break;
        default: break;
    }

    const int bootEvent = pollButton(btnBoot, PIN_BTN_BOOT);
    if (bootEvent) awakeUntil = millis() + AWAKE_BUTTON_MS;
    switch (bootEvent) {
        case 1:
            showStatus = !showStatus;
            statusUntil = now + 8000;
            lastStatusUi = 0;
            dirty = true;
            break;
        case 2:
            invert = !invert;
            prefs.putBool("invert", invert);
            needFullFrame = true;
            Serial.printf("[boot] инверсия: %d\n", invert);
            dirty = true;
            break;
        default: break;
    }

    updateTime();

    // Wi-Fi может не подняться с первого раза (роутер отвечает AUTH_FAIL и т.п.).
    // Пока бодрствуем — периодически пробуем ещё, иначе плата до следующего
    // пробуждения остаётся без сети.
    if (!net::wifiConnected() && now - lastWifiTry > WIFI_RETRY_MS) {
        lastWifiTry = now;
        Serial.println("[wifi] повторная попытка подключения");
        state.wifiOk = net::wifiConnect(15000);
        net::ipString(ipBuf, sizeof(ipBuf));
        if (state.wifiOk) {
            Serial.printf("[wifi] ok, ip %s, rssi %d\n", ipBuf, (int)net::rssi());
            state.rssi = net::rssi();
            ensureServices();
            if (!state.timeSynced && net::timeSync(10000)) {
                state.timeSynced = true;
                saveRtcFromSystem();
            }
            refreshWeather();
            dirty = true;
        }
    }

    // сетевые сервисы: HTTP API, mDNS, OTA
    if (net::wifiConnected()) {
        ensureServices();
        const uint32_t left = awakeUntil > millis() ? (awakeUntil - millis()) / 1000 : 0;
        services::publish(&state, weatherAt, secondsUntilNextWake(), left);
        services::loop();
    }
    if (refreshNow) {
        refreshNow = false;
        Serial.println("[http] запрошено обновление погоды");
        refreshWeather();
        dirty = true;
    }

    if (now - lastSensor > SENSOR_PERIOD_MS) {
        lastSensor = now;
        readSensors();
    }

    if (net::wifiConnected() && now - lastWeather > WEATHER_PERIOD_MS) {
        refreshWeather();
    }

    if (!state.timeSynced && net::wifiConnected() && now - lastNtp > NTP_RETRY_MS) {
        lastNtp = now;
        if (net::timeSync(8000)) {
            state.timeSynced = true;
            saveRtcFromSystem();
            dirty = true;
        }
    }

    if (showStatus && now > statusUntil) {
        showStatus = false;
        dirty = true;
    }
    if (showStatus && now - lastStatusUi > 1000) {
        lastStatusUi = now;
        dirty = true;
    }

    if (screen == SCREEN_FORECAST && !showStatus && now > forecastUntil) {
        screen = SCREEN_MAIN;
        dirty = true;
    }

    // Любая активность в консоли отключает сон до перезагрузки — удобно при отладке.
    if (Serial.available()) {
        const int c = Serial.read();
        if (!stayAwake) {
            stayAwake = true;
            Serial.println("[sleep] сон отключён до перезагрузки");
        }
        if (c == 's') {
            dumpFb();
        } else if (c == 't') {
            showStatus = !showStatus;
            statusUntil = millis() + 60000;
            dirty = true;
        } else if (c == 'f') {
            screen = (screen == SCREEN_FORECAST) ? SCREEN_MAIN : SCREEN_FORECAST;
            forecastUntil = millis() + 600000;
            showStatus = false;
            dirty = true;
        } else if (c == 'i') {
            invert = !invert;
            needFullFrame = true;
            dirty = true;
        } else if (c == 'p') {
            // самопроверка частичного обновления: перерисовать все зоны полосами
            needFullFrame = false;
            forceMask = 0x3F;
            dirty = true;
            Serial.println("[test] частичная перерисовка всех зон");
        }
    }

    if (dirty) draw();

    if (!stayAwake && sleepEnabled && !services::holdOn() && millis() > awakeUntil) goToSleep();

    delay(40);
}