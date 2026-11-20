#pragma once

#include <stdint.h>
#include <time.h>

#include "ui.h"

// Сетевой слой: то, к чему будет подключаться внешний сервер.
//
// Сейчас это:
//   * mDNS-имя <MDNS_HOSTNAME>.local
//   * HTTP JSON API: /status, /forecast, /refresh, /sleep
//   * OTA-обновление прошивки по Wi-Fi (порт 3232)
//
// Задел под будущую интеграцию: сюда же добавляются MQTT-клиент или нативный
// ESPHome API (protobuf, порт 6053) как отдельные функции в begin()/loop().
// Наружу торчат только publish() и holdOn(), остальное внутреннее.
namespace services {

struct Handlers {
    void (*extendAwake)();            // продлить бодрствование (пришёл запрос)
    void (*setSleep)(bool enabled);   // включить/выключить глубокий сон
    void (*requestRefresh)();         // принудительно обновить погоду
    bool (*sleepEnabled)();
    void (*partialTest)();            // перерисовать экран полосами (диагностика)
};

void begin(const Handlers& h);
void loop();

// Сообщить сервисам актуальное состояние (указатель живёт всё время работы).
void publish(const ui::State* state, time_t weatherAt, uint32_t nextWakeS,
             uint32_t awakeLeftS);

// true — уходить в сон нельзя (идёт OTA).
bool holdOn();

bool otaRunning();

}  // namespace services