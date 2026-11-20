#include "panel.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "config.h"

namespace {

constexpr int W = 400;
constexpr int H = 300;
constexpr int FB_BYTES = W * H / 8;   // 15000
constexpr int ROW_BYTES = W / 8;      // 50
constexpr int PAIR_ROWS = H / 4;      // 75 байт на пару столбцов
constexpr int PAIRS = W / 2;          // 200 пар
constexpr uint32_t SPI_HZ = 10 * 1000 * 1000;

// Формат панели: byte_x = x/2, block_y = (H-1-y)/4, index = byte_x*75 + block_y.
// 1 = белый, 0 = чёрный (обратно логическому кадру).
uint8_t panel_buf[FB_BYTES];

void csLow() { digitalWrite(PIN_LCD_CS, LOW); }
void csHigh() { digitalWrite(PIN_LCD_CS, HIGH); }

void cmd(uint8_t c) {
    digitalWrite(PIN_LCD_DC, LOW);
    csLow();
    SPI.transfer(c);
    csHigh();
}

void data(const uint8_t* d, size_t n) {
    digitalWrite(PIN_LCD_DC, HIGH);
    csLow();
    // Крупный кадр отправляем кусками: буфер SPI-транзакции ограничен.
    constexpr size_t CHUNK = 2048;
    for (size_t off = 0; off < n; off += CHUNK) {
        const size_t len = (n - off < CHUNK) ? (n - off) : CHUNK;
        SPI.writeBytes(d + off, len);
    }
    csHigh();
}

void data1(uint8_t v) { data(&v, 1); }

void reset() {
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(50);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(20);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(50);
}

// Упаковка полосы пар столбцов [p0..p1] из логического кадра в panel_buf.
void packColumns(const uint8_t* fb, int p0, int p1, bool invert) {
    for (int p = p0; p <= p1; ++p) {
        const int x = p * 2;
        for (int y = 0; y < H; ++y) {
            const int inv_y = H - 1 - y;
            const int block_y = inv_y >> 2;
            const int ly = inv_y & 3;
            const uint8_t bits = fb[y * ROW_BYTES + (x >> 3)];
            const bool ink0 = bits & (0x80 >> (x & 7));
            const bool ink1 = bits & (0x80 >> ((x + 1) & 7));
            uint8_t& dst = panel_buf[p * PAIR_ROWS + block_y];
            const uint8_t m0 = (uint8_t)(0x80 >> (ly * 2));
            const uint8_t m1 = (uint8_t)(0x80 >> (ly * 2 + 1));
            if (ink0) dst &= (uint8_t)~m0; else dst |= m0;
            if (ink1) dst &= (uint8_t)~m1; else dst |= m1;
        }
    }

    if (invert) {
        const int from = p0 * PAIR_ROWS;
        const int to = (p1 + 1) * PAIR_ROWS;
        for (int i = from; i < to; ++i) panel_buf[i] = (uint8_t)~panel_buf[i];
    }
}

}  // namespace

namespace panel {

void init() {
    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_DC, OUTPUT);
    pinMode(PIN_LCD_RST, OUTPUT);
    csHigh();

    SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, -1);
    SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));

    reset();

    cmd(0xD6); data1(0x17); data1(0x02);          // NVM load control

    cmd(0xD1); data1(0x01);                       // booster enable

    cmd(0xC0); data1(0x11); data1(0x04);          // gate voltage
    cmd(0xC1); data1(0x41); data1(0x41); data1(0x41); data1(0x41);  // VSHP
    cmd(0xC2); data1(0x19); data1(0x19); data1(0x19); data1(0x19);
    cmd(0xC4); data1(0x41); data1(0x41); data1(0x41); data1(0x41);
    cmd(0xC5); data1(0x19); data1(0x19); data1(0x19); data1(0x19);

    cmd(0xD8); data1(0xA6); data1(0xE9);

    cmd(0xB2); data1(0x05);

    cmd(0xB3);
    data1(0xE5); data1(0xF6); data1(0x05); data1(0x46); data1(0x77);
    data1(0x77); data1(0x77); data1(0x77); data1(0x76); data1(0x45);

    cmd(0xB4);
    data1(0x05); data1(0x46); data1(0x77); data1(0x77); data1(0x77);
    data1(0x77); data1(0x76); data1(0x45);

    cmd(0x62); data1(0x32); data1(0x03); data1(0x1F);

    cmd(0xB7); data1(0x13);
    cmd(0xB0); data1(0x64);

    cmd(0x11);                                    // sleep out
    delay(200);
    cmd(0xC9); data1(0x00);

    cmd(0x36); data1(0x48);                       // MADCTL
    cmd(0x3A); data1(0x11);                       // 1 бит на пиксель

    cmd(0xB9); data1(0x20);
    cmd(0xB8); data1(0x29);

    cmd(0x21);                                    // инверсия: 1 = белый
    cmd(0x2A); data1(0x12); data1(0x2A);          // окно по X
    cmd(0x2B); data1(0x00); data1(0xC7);          // окно по Y
    cmd(0x35); data1(0x00);
    cmd(0xD0); data1(0xFF);
    cmd(0x38);
    cmd(0x29);                                    // display on

    memset(panel_buf, 0xFF, sizeof(panel_buf));
}

void flushX(const uint8_t* fb, int x1, int x2, bool invert) {
    if (x1 > x2) { const int t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 > W - 1) x2 = W - 1;
    if (x1 > x2) return;

    int p0 = x1 >> 1;
    int p1 = x2 >> 1;
    if (p1 > PAIRS - 1) p1 = PAIRS - 1;

    packColumns(fb, p0, p1, invert);

    cmd(0x2A); data1(0x12); data1(0x2A);          // X-окно всегда полное
    cmd(0x2B); data1((uint8_t)p0); data1((uint8_t)p1);
    cmd(0x2C);
    data(panel_buf + p0 * PAIR_ROWS, (size_t)(p1 - p0 + 1) * PAIR_ROWS);
}

void flush(const uint8_t* fb, bool invert) {
    flushX(fb, 0, W - 1, invert);
}

}  // namespace panel