"""Зонд: следит, появляется ли плата в сети или на USB, и печатает переходы.

Запуск: python tools/probe.py rlcd-clock.local 300
"""

import socket
import sys
import time

from serial.tools import list_ports

HOST = sys.argv[1] if len(sys.argv) > 1 else "rlcd-clock.local"
SECONDS = int(sys.argv[2]) if len(sys.argv) > 2 else 300


def http_ok(host, timeout=0.4):
    try:
        s = socket.create_connection((host, 80), timeout=timeout)
        s.close()
        return True
    except OSError:
        return False


def usb_ports():
    return {p.device for p in list_ports.comports() if "USB" in (p.description or "")}


deadline = time.time() + SECONDS
prev_http = None
prev_ports = None
last_note = 0

print(f"probing {HOST}:80 and USB for {SECONDS}s", flush=True)

while time.time() < deadline:
    h = http_ok(HOST)
    ports = usb_ports()

    if h != prev_http:
        print(f"{time.strftime('%H:%M:%S')} http:{'UP' if h else 'down'}", flush=True)
        prev_http = h
    if ports != prev_ports:
        print(f"{time.strftime('%H:%M:%S')} usb:{sorted(ports) or 'none'}", flush=True)
        prev_ports = ports
    if time.time() - last_note > 15:
        last_note = time.time()
        print(f"{time.strftime('%H:%M:%S')} ... http={'up' if h else 'down'} usb={sorted(ports) or 'none'}",
              flush=True)

    time.sleep(0.7)

print("probe finished", flush=True)
