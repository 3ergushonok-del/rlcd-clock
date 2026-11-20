#include "hw.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

constexpr uint8_t RTC_ADDR = 0x51;
constexpr uint8_t SHTC3_ADDR = 0x70;

bool i2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t* out, size_t len) {
    if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; ++i) out[i] = Wire.read();
    return true;
}

uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

}  // namespace

namespace hw {

void begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    // Батарея
    analogReadResolution(12);
    (void)analogRead(PIN_BAT_ADC);          // инициализирует канал АЦП
    analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
}

bool rtcRead(struct tm* out, uint8_t* rawOut) {
    const uint8_t reg = 0x04;
    if (!i2cWrite(RTC_ADDR, &reg, 1)) return false;

    uint8_t d[7] = {0};
    if (!i2cRead(RTC_ADDR, d, 7)) return false;
    if (rawOut) memcpy(rawOut, d, 7);

    const bool valid = (d[0] & 0x80) == 0;   // бит VL: питание пропадало

    out->tm_sec  = bcd2dec(d[0] & 0x7F);
    out->tm_min  = bcd2dec(d[1] & 0x7F);
    out->tm_hour = bcd2dec(d[2] & 0x3F);
    out->tm_mday = bcd2dec(d[3] & 0x3F);
    out->tm_wday = (d[4] & 0x07) % 7;
    out->tm_mon  = (int)bcd2dec(d[5] & 0x1F) - 1;
    out->tm_year = 100 + bcd2dec(d[6]);
    out->tm_isdst = 0;

    if (out->tm_mon < 0 || out->tm_mon > 11) return false;
    return valid;
}

void rtcWrite(const struct tm& t) {
    // Снимаем STOP (бит 5 в Control_1), иначе счёт не идёт.
    uint8_t ctrl[2] = {0x00, 0x00};
    i2cWrite(RTC_ADDR, ctrl, 2);

    uint8_t buf[8];
    buf[0] = 0x04;
    buf[1] = dec2bcd((uint8_t)t.tm_sec);
    buf[2] = dec2bcd((uint8_t)t.tm_min);
    buf[3] = dec2bcd((uint8_t)t.tm_hour);
    buf[4] = dec2bcd((uint8_t)t.tm_mday);
    buf[5] = (uint8_t)(t.tm_wday % 7);
    buf[6] = dec2bcd((uint8_t)(t.tm_mon + 1));
    buf[7] = dec2bcd((uint8_t)(t.tm_year % 100));
    i2cWrite(RTC_ADDR, buf, sizeof(buf));
}

bool shtc3Read(float* tempC, float* rhPct) {
    const uint8_t wake[] = {0x35, 0x17};
    const uint8_t meas[] = {0x78, 0x66};   // T first, без clock stretching
    const uint8_t sleep[] = {0xB0, 0x98};

    if (!i2cWrite(SHTC3_ADDR, wake, 2)) return false;
    delayMicroseconds(300);

    if (!i2cWrite(SHTC3_ADDR, meas, 2)) {
        i2cWrite(SHTC3_ADDR, sleep, 2);
        return false;
    }
    delay(15);

    uint8_t d[6];
    const bool ok = i2cRead(SHTC3_ADDR, d, 6);
    i2cWrite(SHTC3_ADDR, sleep, 2);
    if (!ok) return false;

    const uint16_t rawT = (uint16_t)((d[0] << 8) | d[1]);
    const uint16_t rawH = (uint16_t)((d[3] << 8) | d[4]);

    *tempC = -45.0f + 175.0f * (float)rawT / 65536.0f;
    *rhPct = 100.0f * (float)rawH / 65536.0f;
    return true;
}

float batteryVoltage() {
    int sum = 0;
    for (int i = 0; i < 8; ++i) sum += analogReadMilliVolts(PIN_BAT_ADC);
    return (sum / 8.0f) * 0.001f * BAT_DIVIDER;
}

uint8_t batteryLevel() {
    const float v = batteryVoltage();
    if (v <= BAT_EMPTY_V) return 0;
    if (v >= BAT_FULL_V) return 100;
    return (uint8_t)((v - BAT_EMPTY_V) / (BAT_FULL_V - BAT_EMPTY_V) * 100.0f);
}

}  // namespace hw