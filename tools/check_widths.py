"""Проверка ширины строк в сгенерированных шрифтах (без запуска на плате).

Считает ширину конкретных надписей и сравнивает с доступным местом.
Вывод в ASCII, чтобы не зависеть от кодовой страницы консоли.
"""

import re

src = open("include/fonts.h", encoding="utf-8").read()


def advances(name):
    m = re.search(r"static const Glyph %s_glyphs\[\] = \{(.*?)\n\};" % name, src, re.S).group(1)
    return {int(cp): int(adv) for cp, adv in
            re.findall(r"\{ (\d+), \d+, \d+, -?\d+, -?\d+, (\d+), \w+ \+ \d+ \}", m)}


def width(table, text):
    total = 0
    for ch in text:
        a = table.get(ord(ch))
        if a is None:
            print(f"  !! missing glyph: U+{ord(ch):04X} in {text!r}")
            return None
        total += a
    return total


CHECKS = [
    # (font, string, available width)
    ("font_smallb", "ветер 4.0 м/с · влажн. 70%", 372),
    ("font_smallb", "давл. 1018 гПа · сегодня -2°…-1°", 372),
    ("font_smallb", "в доме 25.4° · 40%", 300),
    ("font_smallb", "снег", 133),
    ("font_smallb", "облачно", 133),
    ("font_smallb", "переменно", 133),
    ("font_smallb", "почти ясно", 133),
    ("font_smallb", "снегопад", 133),
    ("font_text", "СЕГОДНЯ", 133),
    ("font_text", "ПОГОДА НА 3 ДНЯ", 300),
    ("font_text", "переменно", 133),
]

tables = {name: advances(name) for name in ("font_text", "font_small", "font_smallb", "font_temp")}

bad = 0
for font, text, avail in CHECKS:
    w = width(tables[font], text)
    if w is None:
        bad += 1
        continue
    ok = w <= avail
    if not ok:
        bad += 1
    print(f"{'OK  ' if ok else 'OVER'} {font:11s} {w:4d}px / {avail}px  {text}")

print("\nпроблем:", bad)