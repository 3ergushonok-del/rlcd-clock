"""Проверка, что все символы из исходников есть в сгенерированных шрифтах."""

import re
import sys

src = open("include/fonts.h", encoding="utf-8").read()


def chars_of(name):
    m = re.search(r"static const Glyph %s_glyphs\[\] = \{(.*?)\n\};" % name, src, re.S).group(1)
    return {int(cp) for cp in re.findall(r"\{ (\d+),", m)}


text_font = chars_of("font_text")
small_font = chars_of("font_small")
time_font = chars_of("font_time")

ui = open("src/ui.cpp", encoding="utf-8").read()
main = open("src/main.cpp", encoding="utf-8").read()

# строковые литералы
literals = re.findall(r'"((?:[^"\\]|\\.)*)"', ui + main)

missing_text = {}
missing_time = set()

for lit in literals:
    for ch in lit:
        cp = ord(ch)
        if cp < 32 or ch in "\\":
            continue
        if ch.isdigit() or ch in ":- ":
            if cp not in time_font and ch != " ":
                missing_time.add(ch)
        if cp not in text_font:
            missing_text.setdefault(ch, 0)
            missing_text[ch] += 1

print("нет в font_text:", {k: v for k, v in missing_text.items()} or "всё есть")
print("нет в font_time:", missing_time or "всё есть")

# высота заглавных в большом шрифте
m = re.search(r"static const Glyph font_time_glyphs\[\] = \{(.*?)\n\};", src, re.S).group(1)
tops = [int(yo) for _, _, _, _, yo, _, _ in
        re.findall(r"\{ (\d+), (\d+), (\d+), (-?\d+), (-?\d+), (\d+), \w+ \+ (\d+) \}", m)]
print("font_time: верх глифа", min(tops), "-> при baseline 152 время занимает y",
      152 + min(tops), "..152")

if missing_text or missing_time:
    sys.exit(1)