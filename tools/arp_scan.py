"""Пинг-свип подсети и поиск MAC Espressif в ARP-таблице.

Помогает отличить «плата не в сети» от «плата в сети, но HTTP не отвечает».
"""

import re
import subprocess
import sys
import concurrent.futures

PREFIX = sys.argv[1] if len(sys.argv) > 1 else "192.168.1."

# Espressif OUI, которые встречаются чаще всего
ESPRESSIF = ("14:c1:9f", "24:0a:c4", "24:6f:28", "30:ae:a4", "3c:61:05",
             "3c:71:bf", "40:91:51", "44:17:93", "48:27:e2", "4c:75:25",
             "54:5a:a6", "5c:cf:7f", "60:01:94", "68:c6:3a", "7c:9e:bd",
             "7c:df:a1", "80:64:6f", "84:0d:8e", "84:f3:eb", "8c:aa:b5",
             "90:38:0c", "94:b9:7e", "a0:20:a6", "a4:cf:12", "ac:67:b2",
             "b4:e6:2d", "b8:f0:09", "bc:dd:c2", "c4:4f:33", "c8:2b:96",
             "cc:50:e3", "d8:a0:1d", "dc:4f:22", "e8:68:e7", "ec:62:60",
             "f0:08:d1", "f4:cf:a2", "fc:f5:c4")


def ping(ip):
    subprocess.run(["ping", "-n", "1", "-w", "250", ip],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return ip


def main():
    ips = [f"{PREFIX}{i}" for i in range(1, 255)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
        list(ex.map(ping, ips))

    out = subprocess.run(["arp", "-a"], capture_output=True, text=True).stdout
    rows = re.findall(r"(\d+\.\d+\.\d+\.\d+)\s+([0-9a-fA-F-]{17})", out)

    print(f"hosts in ARP: {len(rows)}")
    for ip, mac in rows:
        m = mac.lower().replace("-", ":")
        tag = "ESP?" if m.startswith(ESPRESSIF) else ""
        if tag:
            print(f"  {ip:16s} {m}  {tag}")
    if not any(mac.lower().replace('-', ':').startswith(ESPRESSIF) for _, mac in rows):
        print("  no Espressif MAC found")


if __name__ == "__main__":
    main()