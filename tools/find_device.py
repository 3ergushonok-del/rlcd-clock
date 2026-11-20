"""Ищет плату в локальной сети: сканирует порт 80 и ищет наш /status.

Запуск: python tools/find_device.py [префикс сети]
"""

import concurrent.futures
import json
import re
import socket
import sys
import urllib.request

PREFIX = sys.argv[1] if len(sys.argv) > 1 else "192.168.1."
TIMEOUT = 0.35


def port80(ip):
    try:
        s = socket.create_connection((ip, 80), timeout=TIMEOUT)
        s.close()
        return ip
    except OSError:
        return None


def check(ip):
    try:
        with urllib.request.urlopen(f"http://{ip}/status", timeout=2) as r:
            body = r.read(2048).decode("utf-8", "replace")
            if "rlcd-clock" in body:
                return ip, body
    except Exception:
        return None
    return None


def main():
    ips = [f"{PREFIX}{i}" for i in range(1, 255)]
    print(f"scanning {PREFIX}0/24 on port 80 ...", flush=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=128) as ex:
        open_ips = [ip for ip in ex.map(port80, ips) if ip]

    print(f"open port 80: {open_ips or 'none'}", flush=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=32) as ex:
        found = [r for r in ex.map(check, open_ips) if r]

    if not found:
        print("rlcd-clock NOT found on the LAN", flush=True)
        return

    for ip, body in found:
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            print(f"found at {ip}: {body[:120]}")
            continue
        print(f"FOUND at {ip}")
        print(f"  ip={data.get('ip')} rssi={data.get('rssi')} uptime={data.get('uptime_s')}s "
              f"awake_left={data.get('awake_left_s')}s sleep={data.get('sleep_enabled')}")
        w = data.get("weather", {})
        print(f"  weather valid={w.get('valid')} temp={w.get('temp')} state={w.get('state')}")

    # MAC из таблицы ARP (Espressif OUI 14:c1:9f)
    print("\nARP entries with Espressif OUI:", flush=True)


if __name__ == "__main__":
    main()