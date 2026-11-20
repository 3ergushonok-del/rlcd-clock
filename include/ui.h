#pragma once

#include <stdint.h>

#include "net.h"

namespace ui {

struct State {
    bool    timeValid   = false;
    int     hour        = 0;
    int     minute      = 0;
    int     wday        = 0;   // 0 = воскресенье
    int     mday        = 1;
    int     mon         = 0;   // 0..11

    net::Weather weather;
    net::Day     days[3];

    bool    indoorValid = false;
    float   inTemp      = 0;
    float   inRh        = 0;

    bool    batValid    = false;
    uint8_t batPct      = 0;
    float   batVolt     = 0;

    bool    wifiOk      = false;
    int8_t  rssi        = 0;
    bool    timeSynced  = false;
};

// Зона экрана: прямоугольник, который перерисовывается целиком.
// Частичное обновление панели возможно только полосами на всю высоту,
// поэтому по X зоны не должны пересекаться без нужды.
struct Box {
    int x, y, w, h;
};

// Главный экран. Возвращает маску зон, которые реально перерисованы.
// full — перерисовать всё (после смены экрана, инверсии, загрузки).
// forceMask — принудительно перерисовать указанные зоны (отладка).
uint16_t renderWeather(const State& s, bool full, uint16_t forceMask = 0);

int      regionCount();
Box      regionBox(int i);

void renderForecast(const State& s, const char* updatedAt);
void renderBoot(const char* title, const char* subtitle);
void renderStatus(const State& s, const char* ip, uint32_t uptimeS);

}  // namespace ui