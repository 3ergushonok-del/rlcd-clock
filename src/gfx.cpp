#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gfx {

uint8_t fb[BYTES];

namespace {

const Glyph* findGlyph(const Font& f, uint16_t cp) {
    int lo = 0, hi = (int)f.count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (f.glyphs[mid].cp == cp) return &f.glyphs[mid];
        if (f.glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

// Декодирование UTF-8 -> Unicode. Возвращает длину последовательности.
int utf8Next(const char* s, uint16_t* out) {
    const uint8_t* p = (const uint8_t*)s;
    if (p[0] < 0x80) { *out = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *out = (uint16_t)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *out = (uint16_t)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        return 3;
    }
    *out = '?';
    return 1;
}

}  // namespace

void clear(bool ink) { memset(fb, ink ? 0xFF : 0x00, BYTES); }

void pixel(int x, int y, bool ink) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t& b = fb[y * ROW + (x >> 3)];
    if (ink) b |= (uint8_t)(0x80 >> (x & 7));
    else     b &= (uint8_t)~(0x80 >> (x & 7));
}

void hline(int x0, int x1, int y, bool ink) {
    if (y < 0 || y >= H) return;
    if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;
    for (int x = x0; x <= x1; ++x) pixel(x, y, ink);
}

void vline(int x, int y0, int y1, bool ink) {
    if (x < 0 || x >= W) return;
    if (y0 > y1) { const int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; ++y) pixel(x, y, ink);
}

void rect(int x, int y, int w, int h, bool ink) {
    if (w <= 0 || h <= 0) return;
    hline(x, x + w - 1, y, ink);
    hline(x, x + w - 1, y + h - 1, ink);
    vline(x, y, y + h - 1, ink);
    vline(x + w - 1, y, y + h - 1, ink);
}

void fillRect(int x, int y, int w, int h, bool ink) {
    if (w <= 0 || h <= 0) return;
    for (int j = y; j < y + h; ++j) hline(x, x + w - 1, j, ink);
}

void line(int x0, int y0, int x1, int y1, bool ink) {
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, ink);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int r, bool ink) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pixel(cx + x, cy + y, ink); pixel(cx + y, cy + x, ink);
        pixel(cx - y, cy + x, ink); pixel(cx - x, cy + y, ink);
        pixel(cx - x, cy - y, ink); pixel(cx - y, cy - x, ink);
        pixel(cx + y, cy - x, ink); pixel(cx + x, cy - y, ink);
        ++y;
        if (err < 0) err += 2 * y + 1; else { --x; err += 2 * (y - x) + 1; }
    }
}

void fillCircle(int cx, int cy, int r, bool ink) {
    for (int dy = -r; dy <= r; ++dy) {
        const int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        hline(cx - dx, cx + dx, cy + dy, ink);
    }
}

int textWidth(const Font& f, const char* s) {
    int w = 0;
    uint16_t cp;
    while (*s) {
        s += utf8Next(s, &cp);
        const Glyph* g = findGlyph(f, cp);
        w += g ? g->adv : (f.glyphs[0].adv);
    }
    return w;
}

int textHeight(const Font& f) {
    int h = 0;
    for (uint16_t i = 0; i < f.count; ++i) {
        const int top = -f.glyphs[i].yo;
        if (top > h) h = top;
    }
    return h;
}

void text(const Font& f, int x, int baseline, const char* s, bool ink) {
    int pen = x;
    uint16_t cp;
    while (*s) {
        s += utf8Next(s, &cp);
        const Glyph* g = findGlyph(f, cp);
        if (!g) continue;
        if (g->w && g->h) {
            const int stride = (g->w + 7) / 8;
            const int ox = pen + g->xo;
            const int oy = baseline + g->yo;
            for (int j = 0; j < g->h; ++j) {
                const uint8_t* row = g->bmp + j * stride;
                for (int i = 0; i < g->w; ++i) {
                    if (row[i >> 3] & (0x80 >> (i & 7))) pixel(ox + i, oy + j, ink);
                }
            }
        }
        pen += g->adv;
    }
}

void textCentered(const Font& f, int cx, int baseline, const char* s, bool ink) {
    text(f, cx - textWidth(f, s) / 2, baseline, s, ink);
}

void textRight(const Font& f, int xRight, int baseline, const char* s, bool ink) {
    text(f, xRight - textWidth(f, s), baseline, s, ink);
}

}  // namespace gfx