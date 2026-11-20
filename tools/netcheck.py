import socket
import ssl

HOSTS = [
    ("api.open-meteo.com", 443),
    ("api.open-meteo.com", 80),
    ("wttr.in", 443),
    ("api.met.no", 443),
    ("api.openweathermap.org", 443),
    ("www.7timer.info", 443),
    ("api.weather.gov", 443),
]

for host, port in HOSTS:
    try:
        ip = socket.gethostbyname(host)
    except Exception as e:
        print(f"{host}:{port} DNS FAIL {e}")
        continue
    try:
        s = socket.create_connection((ip, port), timeout=5)
        s.close()
        tcp = "tcp ok"
    except Exception as e:
        print(f"{host}:{port} ({ip}) -> {type(e).__name__}")
        continue

    if port == 443:
        try:
            ctx = ssl.create_default_context()
            with socket.create_connection((ip, 443), timeout=6) as raw:
                with ctx.wrap_socket(raw, server_hostname=host) as tls:
                    print(f"{host}:443 ({ip}) -> {tcp}, tls ok {tls.version()}")
        except Exception as e:
            print(f"{host}:443 ({ip}) -> {tcp}, tls FAIL {type(e).__name__}: {e}")
    else:
        print(f"{host}:{port} ({ip}) -> {tcp}")