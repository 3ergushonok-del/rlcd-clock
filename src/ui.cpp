#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "gfx.h"

namespace ui {

namespace {

constexpr int MARGIN    = 14;
constexpr int RIGHT     = gfx::W - MARGIN;   // 386
constexpr int HEADER_Y  = 34;
constexpr int RULE_TOP  = 46;

// Главный экран
constexpr int TEMP_X    = 26;
constexpr int TEMP_BASE = 150;
constexpr int FEELS_BASE = 190;
constexpr int COND_BASE = 170;
constexpr int ICON_CX   = 302;
constexpr int ICON_CY   = 106;
constexpr int ICON_SCALE = 200;   // иконка на главном экране вдвое крупнее
constexpr int RULE_MID  = 208;
constexpr int DET1_Y    = 232;
constexpr int DET2_Y    = 254;
constexpr int RULE_BOT  = 266;
constexpr int FOOT_Y    = 288;

// Экран прогноза
constexpr int DAY_LABEL_Y = 82;
constexpr int DAY_DATE_Y  = 102;
constexpr int DAY_ICON_Y  = 148;
constexpr int DAY_TEMP_Y  = 214;
constexpr int DAY_COND_Y  = 240;
constexpr int FORECAST_RULE = 256;

const char* const WEEKDAY[7] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
const char* const MONTH[12] = {"января", "февраля", "марта",     "апреля",
                               "мая",    "июня",    "июля",      "августа",
                               "сентября", "октября", "ноября",  "декабря"};

// Иконки рисуются в системе координат с масштабом: k = 100 — базовый размер.
struct Pen {
    int cx, cy, k;
    int X(int dx) const { return cx + dx * k / 100; }
    int Y(int dy) const { return cy + dy * k / 100; }
    int L(int v)  const { return v * k / 100; }
};

void iconSun(const Pen& p, int r) {
    gfx::fillCircle(p.cx, p.cy, p.L(r));
    for (int a = 0; a < 360; a += 45) {
        const float rad = (float)a * 0.0174533f;
        const float c = cosf(rad), s = sinf(rad);
        const int ix = (int)(c * (r + 3)), iy = (int)(s * (r + 3));
        const int ox = (int)(c * (r + 10)), oy = (int)(s * (r + 10));
        gfx::line(p.X(ix), p.Y(iy), p.X(ox), p.Y(oy));
    }
}

void iconCloud(const Pen& p) {
    gfx::fillCircle(p.X(-12), p.Y(4), p.L(8));
    gfx::fillCircle(p.X(2), p.Y(-3), p.L(11));
    gfx::fillCircle(p.X(15), p.Y(4), p.L(7));
    gfx::fillRect(p.X(-20), p.Y(4), p.L(36), p.L(7));
}

void iconPartly(const Pen& p) {
    iconSun(Pen{p.X(-11), p.Y(-11), p.k}, 8);
    gfx::fillRect(p.X(-18), p.Y(2), p.L(19), p.L(9), false);   // «вырезаем» солнце под облаком
    iconCloud(Pen{p.X(2), p.Y(6), p.k});
}

void iconRain(const Pen& p) {
    iconCloud(Pen{p.cx, p.Y(-8), p.k});
    for (int i = -1; i <= 1; ++i) {
        const int x = p.X(i * 9);
        gfx::line(x, p.Y(12), x - p.L(4), p.Y(22));
    }
}

void iconSnow(const Pen& p) {
    iconCloud(Pen{p.cx, p.Y(-8), p.k});
    for (int i = -1; i <= 1; ++i) {
        const int x = p.X(i * 10);
        const int y = p.Y(17);
        const int a = p.L(4);
        const int b = p.L(2);
        gfx::vline(x, y - a, y + a);
        gfx::hline(x - a, x + a, y);
        gfx::pixel(x - b, y - b); gfx::pixel(x + b, y + b);
        gfx::pixel(x + b, y - b); gfx::pixel(x - b, y + b);
    }
}

void iconThunder(const Pen& p) {
    iconCloud(Pen{p.cx, p.Y(-8), p.k});
    gfx::line(p.X(-2), p.Y(12), p.X(5), p.Y(12));
    gfx::line(p.X(5), p.Y(12), p.X(-3), p.Y(24));
    gfx::line(p.X(-3), p.Y(24), p.X(3), p.Y(24));
}

void iconFog(const Pen& p) {
    iconCloud(Pen{p.cx, p.Y(-8), p.k});
    gfx::hline(p.X(-20), p.X(20), p.Y(13));
    gfx::hline(p.X(-14), p.X(16), p.Y(19));
    gfx::hline(p.X(-20), p.X(20), p.Y(25));
}

void iconByCode(int code, int cx, int cy, int k) {
    const int nudge = -2 * k / 100;
    if (code < 0)                  { iconCloud(Pen{cx, cy, k}); return; }
    if (code <= 1)                 { iconSun(Pen{cx, cy - 4 * k / 100, k}, 13); return; }
    if (code == 2)                 { iconPartly(Pen{cx, cy + nudge, k}); return; }
    if (code == 3)                 { iconCloud(Pen{cx, cy + nudge, k}); return; }
    if (code == 45 || code == 48)  { iconFog(Pen{cx, cy + nudge, k}); return; }
    if (code >= 51 && code <= 67)  { iconRain(Pen{cx, cy + nudge, k}); return; }
    if (code >= 71 && code <= 77)  { iconSnow(Pen{cx, cy + nudge, k}); return; }
    if (code >= 80 && code <= 82)  { iconRain(Pen{cx, cy + nudge, k}); return; }
    if (code == 85 || code == 86)  { iconSnow(Pen{cx, cy + nudge, k}); return; }
    if (code >= 95)                { iconThunder(Pen{cx, cy + nudge, k}); return; }
    iconCloud(Pen{cx, cy + nudge, k});
}

const char* wmoText(int code) {
    if (code < 0)                  return "нет данных";
    if (code == 0)                 return "ясно";
    if (code == 1)                 return "почти ясно";
    if (code == 2)                 return "переменно";
    if (code == 3)                 return "облачно";
    if (code == 45 || code == 48)  return "туман";
    if (code >= 51 && code <= 57)  return "морось";
    if (code >= 61 && code <= 67)  return "дождь";
    if (code >= 71 && code <= 77)  return "снег";
    if (code >= 80 && code <= 82)  return "ливень";
    if (code == 85 || code == 86)  return "снегопад";
    if (code >= 95)                return "гроза";
    return "облачно";
}

void batteryIcon(int x, int y, int pct, bool valid) {
    constexpr int W = 26, H = 13;
    gfx::rect(x, y, W, H);
    gfx::fillRect(x + W, y + 4, 3, 5);
    if (!valid) return;
    const int inner = W - 4;
    const int fill = inner * pct / 100;
    if (fill > 0) gfx::fillRect(x + 2, y + 2, fill, H - 4);
}

// Зоны главного экрана. Частичное обновление панели возможно только полосами
// на всю высоту, поэтому зоны разнесены по X так, чтобы меняться независимо.
constexpr Box BOX_DATE    {12, 12, 180, 34};
constexpr Box BOX_TEMP    {20, 52, 220, 148};
constexpr Box BOX_ICON    {250, 48, 110, 130};
constexpr Box BOX_DETAILS {12, 210, 260, 54};
constexpr Box BOX_INDOOR  {12, 270, 200, 28};
constexpr Box BOX_BATTERY {276, 268, 114, 32};

const Box* const BOXES[] = {&BOX_DATE,  &BOX_TEMP,  &BOX_ICON,
                            &BOX_DETAILS, &BOX_INDOOR, &BOX_BATTERY};
constexpr int REGIONS = 6;

void eraseBox(const Box& b) { gfx::fillRect(b.x, b.y, b.w, b.h, false); }

// Что было нарисовано в прошлый раз: перерисовываем только изменившееся.
struct Cache {
    char date[64];
    char temp[24];
    char feels[24];
    char det1[80];
    char det2[80];
    char indoor[48];
    int  code;
    int  batPct;
    bool batValid;
    bool weatherValid;
};

Cache cache;

void cacheReset() {
    cache.date[0] = cache.temp[0] = cache.feels[0] = '\0';
    cache.det1[0] = cache.det2[0] = cache.indoor[0] = '\0';
    cache.code = -999;
    cache.batPct = -1;
    cache.batValid = false;
    cache.weatherValid = false;
}

void copyStr(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src); }

void screenHeader(const char* title) {
    gfx::text(font_text, MARGIN, HEADER_Y, title);
    gfx::textRight(font_small, RIGHT, HEADER_Y, CITY_NAME);
    gfx::hline(MARGIN, RIGHT, RULE_TOP);
}

void drawBattery(int pct, bool valid) {
    char buf[16];
    batteryIcon(RIGHT - 59, FOOT_Y - 11, pct, valid);
    if (valid) snprintf(buf, sizeof(buf), "%d%%", pct);
    else       snprintf(buf, sizeof(buf), "--%%");
    gfx::textRight(font_smallb, RIGHT - 66, FOOT_Y, buf);
}

}  // namespace

int regionCount() { return REGIONS; }

Box regionBox(int i) {
    if (i < 0 || i >= REGIONS) return Box{0, 0, 0, 0};
    return *BOXES[i];
}

uint16_t renderWeather(const State& s, bool full, uint16_t forceMask) {
    if (full) {
        gfx::clear();
        cacheReset();
    }

    uint16_t mask = 0;
    char buf[80];

    if (full) {
        gfx::textRight(font_small, RIGHT, HEADER_Y, CITY_NAME);
        gfx::hline(MARGIN, RIGHT, RULE_TOP);
        gfx::hline(MARGIN, RIGHT, RULE_MID);
        gfx::hline(MARGIN, RIGHT, RULE_BOT);
    }

    // --- 0: дата ---
    snprintf(buf, sizeof(buf), "%s, %d %s", WEEKDAY[s.wday], s.mday, MONTH[s.mon]);
    if (full || (forceMask & (1u << 0)) || strcmp(buf, cache.date) != 0) {
        copyStr(cache.date, sizeof(cache.date), buf);
        eraseBox(BOX_DATE);
        gfx::text(font_text, MARGIN, HEADER_Y, buf);
        mask |= 1u << 0;
    }

    // --- 1: температура и «ощущается» ---
    if (s.weather.valid) {
        char feels[24];
        snprintf(buf, sizeof(buf), "%d°", (int)lroundf(s.weather.temp));
        snprintf(feels, sizeof(feels), "ощущ. %d°", (int)lroundf(s.weather.feels));
        const bool changed = !cache.weatherValid ||
                             strcmp(buf, cache.temp) != 0 ||
                             strcmp(feels, cache.feels) != 0;
        if (full || (forceMask & (1u << 1)) || changed) {
            copyStr(cache.temp, sizeof(cache.temp), buf);
            copyStr(cache.feels, sizeof(cache.feels), feels);
            eraseBox(BOX_TEMP);
            gfx::text(font_temp, TEMP_X, TEMP_BASE, buf);
            gfx::text(font_text, TEMP_X + 4, FEELS_BASE, feels);
            mask |= 1u << 1;
        }
    } else if (full || (forceMask & (1u << 1)) || cache.weatherValid) {
        cache.temp[0] = cache.feels[0] = '\0';
        eraseBox(BOX_TEMP);
        gfx::text(font_text, TEMP_X, 100, "погода");
        gfx::text(font_text, TEMP_X, 128, "недоступна");
        mask |= 1u << 1;
    }
    cache.weatherValid = s.weather.valid;

    // --- 2: иконка и описание ---
    const int codeNow = s.weather.valid ? s.weather.code : -1;
    if (full || (forceMask & (1u << 2)) || codeNow != cache.code) {
        cache.code = codeNow;
        eraseBox(BOX_ICON);
        iconByCode(codeNow, ICON_CX, ICON_CY, ICON_SCALE);
        if (s.weather.valid) {
            gfx::textCentered(font_text, ICON_CX, COND_BASE, wmoText(codeNow));
        }
        mask |= 1u << 2;
    }

    // --- 3: детали ---
    char d1[80] = "";
    char d2[80] = "";
    if (s.weather.valid) {
        snprintf(d1, sizeof(d1), "ветер %.1f м/с · влажн. %d%%",
                 (double)s.weather.wind, s.weather.humidity);
        snprintf(d2, sizeof(d2), "давл. %d гПа · сегодня %d°…%d°",
                 (int)lroundf(s.weather.pressure),
                 (int)lroundf(s.weather.tmin), (int)lroundf(s.weather.tmax));
    }
    if (full || (forceMask & (1u << 3)) ||
        strcmp(d1, cache.det1) != 0 || strcmp(d2, cache.det2) != 0) {
        copyStr(cache.det1, sizeof(cache.det1), d1);
        copyStr(cache.det2, sizeof(cache.det2), d2);
        eraseBox(BOX_DETAILS);
        if (d1[0]) gfx::text(font_smallb, MARGIN, DET1_Y, d1);
        if (d2[0]) gfx::text(font_smallb, MARGIN, DET2_Y, d2);
        mask |= 1u << 3;
    }

    // --- 4: в доме ---
    char indoor[48];
    if (s.indoorValid) {
        snprintf(indoor, sizeof(indoor), "в доме %.1f° · %d%%",
                 (double)s.inTemp, (int)lroundf(s.inRh));
    } else {
        snprintf(indoor, sizeof(indoor), "в доме --");
    }
    if (full || (forceMask & (1u << 4)) || strcmp(indoor, cache.indoor) != 0) {
        copyStr(cache.indoor, sizeof(cache.indoor), indoor);
        eraseBox(BOX_INDOOR);
        gfx::text(font_smallb, MARGIN, FOOT_Y, indoor);
        mask |= 1u << 4;
    }

    // --- 5: батарея ---
    if (full || (forceMask & (1u << 5)) ||
        s.batPct != cache.batPct || s.batValid != cache.batValid) {
        cache.batPct = s.batPct;
        cache.batValid = s.batValid;
        eraseBox(BOX_BATTERY);
        drawBattery(s.batPct, s.batValid);
        mask |= 1u << 5;
    }

    return mask;
}

void renderForecast(const State& s, const char* updatedAt) {
    gfx::clear();
    screenHeader("ПОГОДА НА 3 ДНЯ");

    gfx::vline(133, 54, 250);
    gfx::vline(266, 54, 250);

    const int cx[3] = {73, 200, 327};
    char buf[48];

    for (int i = 0; i < 3; ++i) {
        const net::Day& d = s.days[i];
        if (!d.valid) {
            gfx::textCentered(font_small, cx[i], DAY_ICON_Y, "нет данных");
            continue;
        }

        if (i == 0)      snprintf(buf, sizeof(buf), "СЕГОДНЯ");
        else if (i == 1) snprintf(buf, sizeof(buf), "ЗАВТРА");
        else             snprintf(buf, sizeof(buf), "%s", WEEKDAY[d.wday]);
        gfx::textCentered(font_text, cx[i], DAY_LABEL_Y, buf);

        snprintf(buf, sizeof(buf), "%d.%02d", d.mday, d.mon + 1);
        gfx::textCentered(font_small, cx[i], DAY_DATE_Y, buf);

        iconByCode(d.code, cx[i], DAY_ICON_Y, 100);

        snprintf(buf, sizeof(buf), "%d° / %d°", (int)lroundf(d.tmax), (int)lroundf(d.tmin));
        gfx::textCentered(font_text, cx[i], DAY_TEMP_Y, buf);

        gfx::textCentered(font_smallb, cx[i], DAY_COND_Y, wmoText(d.code));
    }

    gfx::hline(MARGIN, RIGHT, FORECAST_RULE);
    gfx::text(font_small, MARGIN, FOOT_Y, "макс / мин · KEY — назад");
    if (updatedAt && updatedAt[0]) {
        gfx::textRight(font_small, RIGHT, FOOT_Y, updatedAt);
    }
}

void renderBoot(const char* title, const char* subtitle) {
    gfx::clear();
    gfx::rect(10, 10, gfx::W - 20, gfx::H - 20);
    gfx::textCentered(font_text, gfx::W / 2, 140, title);
    if (subtitle) gfx::textCentered(font_small, gfx::W / 2, 176, subtitle);
}

void renderStatus(const State& s, const char* ip, uint32_t uptimeS) {
    gfx::clear();

    gfx::text(font_text, MARGIN, 40, "СОСТОЯНИЕ");
    gfx::hline(MARGIN, RIGHT, 52);

    char buf[80];
    int y = 78;
    const int step = 22;

    snprintf(buf, sizeof(buf), "Wi-Fi: %s", s.wifiOk ? "подключено" : "нет связи");
    gfx::text(font_small, MARGIN, y, buf); y += step;

    snprintf(buf, sizeof(buf), "IP: %s", ip);
    gfx::text(font_small, MARGIN, y, buf); y += step;

    if (s.wifiOk) snprintf(buf, sizeof(buf), "сигнал: %d дБм", (int)s.rssi);
    else          snprintf(buf, sizeof(buf), "сигнал: -");
    gfx::text(font_small, MARGIN, y, buf); y += step;

    snprintf(buf, sizeof(buf), "время: %s", s.timeSynced ? "синхронизировано" : "только RTC");
    gfx::text(font_small, MARGIN, y, buf); y += step;

    if (s.batValid) {
        snprintf(buf, sizeof(buf), "батарея: %.2f В, %d%%", (double)s.batVolt, s.batPct);
    } else {
        snprintf(buf, sizeof(buf), "батарея: -");
    }
    gfx::text(font_small, MARGIN, y, buf); y += step;

    if (s.indoorValid) {
        snprintf(buf, sizeof(buf), "в доме: %.1f°, %d%%",
                 (double)s.inTemp, (int)lroundf(s.inRh));
    } else {
        snprintf(buf, sizeof(buf), "в доме: -");
    }
    gfx::text(font_small, MARGIN, y, buf); y += step;

    snprintf(buf, sizeof(buf), "свободно SRAM: %u КБ", (unsigned)(ESP.getFreeHeap() / 1024));
    gfx::text(font_small, MARGIN, y, buf); y += step;

    snprintf(buf, sizeof(buf), "работает: %lu ч %lu мин",
             (unsigned long)(uptimeS / 3600), (unsigned long)((uptimeS / 60) % 60));
    gfx::text(font_small, MARGIN, y, buf);

    gfx::textRight(font_small, RIGHT, FOOT_Y, "BOOT — назад");
}

}  // namespace ui

