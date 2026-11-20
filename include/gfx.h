#pragma once

#include <stdint.h>

#include "fonts.h"

// Логический монохромный кадр 400x300. 1 = чернила.
namespace gfx {

constexpr int W = 400;
constexpr int H = 300;
constexpr int ROW = W / 8;      // 50 байт в строке
constexpr int BYTES = H * ROW;  // 15000

extern uint8_t fb[BYTES];

void clear(bool ink = false);

void pixel(int x, int y, bool ink = true);
void hline(int x0, int x1, int y, bool ink = true);
void vline(int x, int y0, int y1, bool ink = true);
void rect(int x, int y, int w, int h, bool ink = true);
void fillRect(int x, int y, int w, int h, bool ink = true);
void line(int x0, int y0, int x1, int y1, bool ink = true);
void circle(int cx, int cy, int r, bool ink = true);
void fillCircle(int cx, int cy, int r, bool ink = true);

int textWidth(const Font& f, const char* s);
int textHeight(const Font& f);
void text(const Font& f, int x, int baseline, const char* s, bool ink = true);
void textCentered(const Font& f, int cx, int baseline, const char* s, bool ink = true);
void textRight(const Font& f, int xRight, int baseline, const char* s, bool ink = true);

}  // namespace gfx