#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

struct Weather {
    bool  valid     = false;
    float temp      = 0;   // °C
    float feels     = 0;   // °C, wind chill
    float wind      = 0;   // м/с
    float tmin      = 0;   // °C, сегодня
    float tmax      = 0;   // °C, сегодня
    float pressure  = 0;   // гПа
    int   humidity  = 0;   // %
    int   code      = -1;  // WMO weather code
};

struct Day {
    bool  valid = false;
    int   wday  = 0;
    int   mday  = 1;
    int   mon   = 0;
    float tmax  = 0;
    float tmin  = 0;
    int   code  = 3;
};

void    begin();                       // часовой пояс
bool    wifiConnect(uint32_t timeoutMs);
bool    wifiConnected();
int8_t  rssi();
void    ipString(char* buf, size_t n);
bool    timeSync(uint32_t timeoutMs);  // SNTP
bool    weatherFetch(Weather* now, Day* days, int dayCount);

}  // namespace net