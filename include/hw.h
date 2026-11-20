#pragma once

#include <stdint.h>
#include <time.h>

namespace hw {

void begin();

// PCF85063. false — если флаг VL выставлен (питание RTC пропадало).
// rawOut — сырые 7 байт регистров 0x04..0x0A для диагностики.
bool  rtcRead(struct tm* out, uint8_t* rawOut = nullptr);
void  rtcWrite(const struct tm& t);

// SHTC3
bool  shtc3Read(float* tempC, float* rhPct);

float   batteryVoltage();
uint8_t batteryLevel();

}  // namespace hw