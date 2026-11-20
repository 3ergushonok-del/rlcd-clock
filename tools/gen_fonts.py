"""
Генератор 1-битных шрифтов для ST7305 (400x300, монохром).

Берёт TTF из C:\\Windows\\Fonts, растеризует нужный набор символов
и складывает в include/fonts.h: glyph-таблица + общий blob с битмапами.

Запуск:  python tools/gen_fonts.py
"""

import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "..", "include", "fonts.h")

DIGITS = "0123456789:-"
TEMP = "0123456789-°"

# Символы для текстовых шрифтов: латиница, кириллица, цифры, пунктуация
BASE = "".join(chr(c) for c in range(32, 127))
CYR = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя"
EXTRA = "°·—–№«»…→←↑↓±"
TEXT_CHARS = BASE + CYR + EXTRA

FONTS = [
    # (имя в C, файл, размер px, набор символов, track)
    ("font_time", "segoeuib.ttf", 96, DIGITS, 0),
    ("font_temp", "segoeuib.ttf", 120, TEMP, 0),
    ("font_text", "segoeui.ttf", 21, TEXT_CHARS, 0),
    ("font_small", "segoeui.ttf", 16, TEXT_CHARS, 0),
    ("font_smallb", "segoeuib.ttf", 16, TEXT_CHARS, 0),
]

FONT_DIR = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")


def pack_glyph(img):
    """1-битное изображение -> список байт, stride = (w+7)//8, bit7 = левый пиксель."""
    w, h = img.size
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    px = img.load()
    for y in range(h):
        base = y * stride
        for x in range(w):
            if px[x, y]:
                out[base + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def build(name, ttf, size, chars, track):
    path = os.path.join(FONT_DIR, ttf)
    font = ImageFont.truetype(path, size)
    glyphs = []
    blob = bytearray()
    seen = set()

    for ch in chars:
        if ch in seen:
            continue
        seen.add(ch)
        advance = int(round(font.getlength(ch) + track))

        if ch == " ":
            glyphs.append((ord(ch), 0, 0, 0, 0, advance, 0))
            continue

        bbox = font.getbbox(ch, anchor="ls")
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        if w <= 0 or h <= 0:
            glyphs.append((ord(ch), 0, 0, 0, 0, advance, 0))
            continue

        img = Image.new("1", (w, h), 0)
        ImageDraw.Draw(img).text((-bbox[0], -bbox[1]), ch, font=font, fill=1, anchor="ls")
        data = pack_glyph(img)
        offset = len(blob)
        blob += data

        assert w < 256 and h < 256, f"{ch}: слишком крупный глиф {w}x{h}"
        assert -128 <= bbox[1] < 128, f"{ch}: yo={bbox[1]} не влезает в int8"
        assert advance < 256, f"{ch}: advance={advance}"

        glyphs.append((ord(ch), w, h, bbox[0], bbox[1], advance, offset))

    glyphs.sort(key=lambda g: g[0])
    print(f"{name}: {len(glyphs)} глифов, blob {len(blob)} байт")
    return name, glyphs, blob


def emit(name, glyphs, blob):
    lines = [f"static const uint8_t {name}_blob[] = {{"]
    for i in range(0, len(blob), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in blob[i:i + 16])
        lines.append("    " + chunk + ",")
    lines.append("};")
    lines.append("")

    lines.append(f"static const Glyph {name}_glyphs[] = {{")
    for cp, w, h, xo, yo, adv, off in glyphs:
        lines.append(f"    {{ {cp}, {w}, {h}, {xo}, {yo}, {adv}, {name}_blob + {off} }},")
    lines.append("};")
    lines.append("")

    lines.append(f"static const Font {name} = {{ {name}_glyphs, "
                 f"{len(glyphs)}, {name}_blob }};")
    lines.append("")
    return "\n".join(lines)


def main():
    header = [
        "// Сгенерировано tools/gen_fonts.py — не редактировать руками.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t cp;        // код символа (Unicode)",
        "    uint8_t  w, h;      // размер битмапа",
        "    int8_t   xo, yo;    // смещение от точки привязки (baseline, левый край)",
        "    uint8_t  adv;       // ширина продвижения курсора",
        "    const uint8_t *bmp; // битмап, stride = (w+7)/8, bit7 = левый пиксель, 1 = чернила",
        "} Glyph;",
        "",
        "typedef struct {",
        "    const Glyph   *glyphs;  // отсортированы по cp",
        "    uint16_t       count;",
        "    const uint8_t *blob;",
        "} Font;",
        "",
    ]

    body = []
    for spec in FONTS:
        body.append(emit(*build(*spec)))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(header) + "\n" + "\n".join(body))
    print("записано:", os.path.abspath(OUT))


if __name__ == "__main__":
    main()