#pragma once

#include <stdint.h>

// Драйвер панели ST7305 400x300 (альбомная ориентация).
// Логический кадр — 1 бит на пиксель, 1 = чернила (чёрный).
namespace panel {

void init();

// Полный кадр.
void flush(const uint8_t* fb, bool invert = false);

// Частичное обновление: вертикальная полоса пикселей [x1..x2], на всю высоту.
// Панель адресуется по столбцам (0x2B — номер пары x/2), поэтому полосы
// всегда идут от верха до низа, зато передаются непрерывным куском кадра.
void flushX(const uint8_t* fb, int x1, int x2, bool invert = false);

}  // namespace panel