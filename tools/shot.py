"""
Снимок экрана платы по Serial.

Прошивка по команде 's' выводит кадр (15000 байт) в hex.
Скрипт открывает порт, запрашивает кадр и собирает PNG 400x300.

Запуск:  python tools/shot.py COM11 preview.png
"""

import sys
import time

import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"
OUT = sys.argv[2] if len(sys.argv) > 2 else "preview.png"
# Необязательная команда перед снимком: t — экран состояния, i — инверсия
PRECMD = sys.argv[3] if len(sys.argv) > 3 else ""

W, H = 400, 300
BYTES = W * H // 8
ROW = W // 8


def grab():
    ser = serial.Serial(PORT, 115200, timeout=2)
    ser.setDTR(False)
    ser.setRTS(False)
    time.sleep(0.3)
    ser.reset_input_buffer()

    # Открытие порта может дёрнуть сброс платы — даём ей загрузиться.
    # Любой символ отключает сон в прошивке, иначе плата уснёт прямо во время съёмки.
    ser.write(b"x")
    time.sleep(2.0)
    if PRECMD:
        ser.write(PRECMD.encode())
        time.sleep(1.0)

    ser.write(b"s")

    data = b""
    deadline = time.time() + 20
    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            data += chunk
            if b"END" in data:
                break
        else:
            ser.write(b"s")
    ser.close()

    start = data.find(b"SHOT")
    end = data.find(b"END", start if start >= 0 else 0)
    if start < 0 or end < 0:
        raise SystemExit(f"кадр не получен, пришло {len(data)} байт: {data[:200]!r}")

    hexstr = bytes(c for c in data[start + 4:end] if chr(c) in "0123456789abcdef")
    if len(hexstr) < BYTES * 2:
        raise SystemExit(f"мало данных: {len(hexstr) // 2} из {BYTES} байт")
    return bytes.fromhex(hexstr[: BYTES * 2].decode())


def to_png(fb, out):
    img = Image.new("L", (W, H), 255)
    px = img.load()
    for y in range(H):
        row = fb[y * ROW:(y + 1) * ROW]
        for x in range(W):
            if row[x >> 3] & (0x80 >> (x & 7)):
                px[x, y] = 0
    img.save(out)
    print("сохранено:", out)


if __name__ == "__main__":
    to_png(grab(), OUT)