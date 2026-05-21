"""
wifi.py - Gerenciamento de conectividade Wi-Fi para ESP32-S3 (MicroPython)

Responsabilidades:
- Conectar ao Wi-Fi com timeout e tentativas
- Expor status e propriedades da conexão no serial
- Reconectar automaticamente quando a conexão cair
"""

import network
import time

# Ajuste conforme ambiente (ideal: sobrescrever em secrets.py local, não versionado)
SSID = "Wokwi-GUEST"
PASSWORD = ""

# Política de conexão/reconexão
CONNECT_TIMEOUT_S = 15
RETRY_DELAY_S = 3
MAX_RETRIES = 3
AUTO_RECONNECT_MIN_INTERVAL_MS = 5000

_STA_IF = network.WLAN(network.STA_IF)
_ultimo_reconnect_ms = 0


def _agora_ms():
    return time.ticks_ms()


def _mac_address():
    raw = _STA_IF.config("mac")
    try:
        return ":".join("{:02X}".format(b) for b in raw)
    except Exception:
        return str(raw)


def _ip_config():
    try:
        ip, mask, gw, dns = _STA_IF.ifconfig()
        return {"ip": ip, "mask": mask, "gw": gw, "dns": dns}
    except Exception:
        return {"ip": "?", "mask": "?", "gw": "?", "dns": "?"}


def _status_text(status_code):
    status_map = {
        0: "STAT_IDLE",
        1: "STAT_CONNECTING",
        2: "STAT_WRONG_PASSWORD",
        3: "STAT_NO_AP_FOUND",
        4: "STAT_CONNECT_FAIL",
        5: "STAT_GOT_IP",
    }
    return status_map.get(status_code, "STAT_UNKNOWN({})".format(status_code))


def conectado():
    return _STA_IF.isconnected()


def imprimir_status_rede(prefixo="[WIFI]"):
    cfg = _ip_config()
    print("{} conectado={} status={}({})".format(
        prefixo,
        _STA_IF.isconnected(),
        _STA_IF.status(),
        _status_text(_STA_IF.status()),
    ))
    print("{} ssid={} mac={}".format(prefixo, SSID, _mac_address()))
    print("{} ip={} mask={} gw={} dns={}".format(
        prefixo, cfg["ip"], cfg["mask"], cfg["gw"], cfg["dns"]
    ))


def conectar(ssid=None, senha=None, timeout_s=CONNECT_TIMEOUT_S, tentativas=MAX_RETRIES):
    ssid = ssid or SSID
    senha = senha or PASSWORD

    if not ssid or ssid == "SEU_SSID":
        print("[WIFI][WARN] SSID não configurado. Atualize wifi.py ou use conectar(ssid, senha).")
        return False

    _STA_IF.active(True)

    if _STA_IF.isconnected():
        print("[WIFI] Já conectado.")
        imprimir_status_rede()
        return True

    for tentativa in range(1, tentativas + 1):
        print("[WIFI] Conectando ao SSID '{}' (tentativa {}/{})...".format(ssid, tentativa, tentativas))
        try:
            _STA_IF.disconnect()
        except Exception:
            pass

        _STA_IF.connect(ssid, senha)

        inicio = time.time()
        while (time.time() - inicio) < timeout_s:
            if _STA_IF.isconnected():
                print("[WIFI] Conexão estabelecida.")
                imprimir_status_rede()
                return True
            time.sleep_ms(250)

        print("[WIFI][WARN] Timeout de conexão: status={}({})".format(
            _STA_IF.status(), _status_text(_STA_IF.status())
        ))
        time.sleep(RETRY_DELAY_S)

    print("[WIFI][ERRO] Falha ao conectar após {} tentativas.".format(tentativas))
    imprimir_status_rede(prefixo="[WIFI][FINAL]")
    return False


def garantir_conectividade(ssid=None, senha=None, timeout_s=CONNECT_TIMEOUT_S):
    global _ultimo_reconnect_ms

    if _STA_IF.isconnected():
        return True

    agora = _agora_ms()
    if time.ticks_diff(agora, _ultimo_reconnect_ms) < AUTO_RECONNECT_MIN_INTERVAL_MS:
        return False

    _ultimo_reconnect_ms = agora
    print("[WIFI][RECONN] Link caiu. Iniciando reconexão...")
    ok = conectar(ssid=ssid, senha=senha, timeout_s=timeout_s, tentativas=1)
    if not ok:
        print("[WIFI][RECONN] Reconexão ainda não concluída. Tentará novamente no próximo ciclo.")
    return ok
