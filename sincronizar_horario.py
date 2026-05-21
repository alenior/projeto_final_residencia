"""
sincronizar_horario.py - Sincronização NTP e utilitários de data/hora (MicroPython)
"""

import ntptime
import time

# UTC-3 (Brasil) por padrão; ajuste conforme necessidade
UTC_OFFSET_HOURS = -3
NTP_HOST = "pool.ntp.org"
SYNC_RETRY_MAX = 3
SYNC_RETRY_DELAY_S = 2


def _localtime_from_epoch(epoch):
    return time.localtime(epoch + (UTC_OFFSET_HOURS * 3600))


def agora_tuple_local():
    return _localtime_from_epoch(time.time())


def agora_iso8601():
    ano, mes, dia, hh, mm, ss, _, _ = agora_tuple_local()
    sinal = "+" if UTC_OFFSET_HOURS >= 0 else "-"
    off = abs(UTC_OFFSET_HOURS)
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}{}{:02d}:00".format(
        ano, mes, dia, hh, mm, ss, sinal, off
    )


def dia_atual_str():
    ano, mes, dia, _, _, _, _, _ = agora_tuple_local()
    return "{:04d}-{:02d}-{:02d}".format(ano, mes, dia)


def imprimir_hora_atual(prefixo="[NTP]"):
    print("{} Hora local atual: {}".format(prefixo, agora_iso8601()))


def sincronizar_ntp(host=NTP_HOST, tentativas=SYNC_RETRY_MAX):
    for tentativa in range(1, tentativas + 1):
        try:
            ntptime.host = host
            ntptime.settime()  # Ajusta RTC em UTC
            print("[NTP] Sincronização concluída com '{}' (tentativa {}/{}).".format(
                host, tentativa, tentativas
            ))
            imprimir_hora_atual()
            return True
        except Exception as exc:
            print("[NTP][WARN] Falha na sincronização (tentativa {}/{}): {}".format(
                tentativa, tentativas, exc
            ))
            time.sleep(SYNC_RETRY_DELAY_S)

    print("[NTP][ERRO] Não foi possível sincronizar horário via NTP.")
    return False
