"""Чтение лога с платы в течение N секунд."""

import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

ser = serial.Serial(PORT, 115200, timeout=0.5)
ser.setDTR(False)
ser.setRTS(False)
time.sleep(0.3)
ser.reset_input_buffer()

deadline = time.time() + SECONDS
while time.time() < deadline:
    line = ser.readline()
    if line:
        sys.stdout.write(line.decode("utf-8", "replace"))
        sys.stdout.flush()

ser.close()