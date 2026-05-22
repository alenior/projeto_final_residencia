"""
main.py - Firmware entrypoint para ESP32-S3 (MicroPython)

Objetivos desta versão inicial:
- Inicialização segura do dispositivo
- Identificação do hardware no serial
- Estrutura modular com chamadas para módulos dedicados (ainda com fallback/mocks)
- Loop principal não-bloqueante com watchdog
- Pontos de extensão para automação local e comandos remotos
"""

import gc
import machine
import network
import sys
import time
from ubinascii import hexlify

# -----------------------------------------------------------------------------
# Configurações gerais
# -----------------------------------------------------------------------------

APP_NAME = "EstufaIoT"
APP_VERSION = "0.1.0-main-bootstrap"
LOOP_INTERVAL_MS = 1000
WDT_TIMEOUT_MS = 15000

# GPIOs principais (documentação rápida no ponto de entrada)
PIN_RELE_BOMBA = 47
PIN_RELE_LAMPADA = 48
PIN_PIR = 42
PIN_SOLO_ADC = 41
PIN_LDR_ADC = 1


# -----------------------------------------------------------------------------
# Fallbacks (mocks) para módulos ainda não implementados
# -----------------------------------------------------------------------------

class _FallbackModule:
    def __init__(self, name):
        self.name = name

    def __getattr__(self, attr):
        def _missing(*args, **kwargs):
            print("[MOCK][{}] {}() ainda não implementado".format(self.name, attr))
            return None

        return _missing


def _safe_import(module_name):
    try:
        return __import__(module_name)
    except Exception as exc:
        print("[WARN] Módulo '{}' indisponível: {}".format(module_name, exc))
        return _FallbackModule(module_name)


import wifi
import sincronizar_horario
luminosidade = _safe_import("luminosidade")
rega = _safe_import("rega")
clima = _safe_import("clima")
alarme_predadores = _safe_import("alarme_predadores")
registros_sdcard = _safe_import("registros_sdcard")
captura_de_imagem = _safe_import("captura_de_imagem")
import envio_e_recebimento_nuvem


# -----------------------------------------------------------------------------
# Utilitários de diagnóstico
# -----------------------------------------------------------------------------

def _mac_address():
    try:
        sta_if = network.WLAN(network.STA_IF)
        sta_if.active(True)
        raw = sta_if.config("mac")
        try:
            return hexlify(raw, ":").decode().upper()
        except TypeError:
            mac_hex = hexlify(raw).decode().upper()
            return ":".join([mac_hex[i:i+2] for i in range(0, len(mac_hex), 2)])
    except Exception as exc:
        return "indisponível ({})".format(exc)


def _chip_info():
    info = {
        "platform": getattr(sys, "platform", "?"),
        "python": getattr(sys, "version", "?"),
        "freq_hz": machine.freq(),
        "uid_hex": "?",
        "reset_cause": machine.reset_cause(),
        "wake_reason": machine.wake_reason(),
        "mem_free": gc.mem_free(),
        "mem_alloc": gc.mem_alloc(),
    }

    try:
        info["uid_hex"] = hexlify(machine.unique_id()).decode().upper()
    except Exception:
        pass

    return info


def print_boot_banner():
    print("\n==================== BOOT ====================")
    print("{} {}".format(APP_NAME, APP_VERSION))
    print("Arquivo:", globals().get("__file__", "main.py"))
    print("MAC:", _mac_address())

    info = _chip_info()
    for key in sorted(info.keys()):
        print("{}: {}".format(key, info[key]))

    print("GPIOs críticos -> bomba:{} lâmpada:{} pir:{} solo:{} ldr:{}".format(
        PIN_RELE_BOMBA,
        PIN_RELE_LAMPADA,
        PIN_PIR,
        PIN_SOLO_ADC,
        PIN_LDR_ADC,
    ))
    print("==============================================\n")


# -----------------------------------------------------------------------------
# Bootstrap de módulos
# -----------------------------------------------------------------------------

def setup_watchdog():
    try:
        return machine.WDT(timeout=WDT_TIMEOUT_MS)
    except Exception as exc:
        print("[WARN] Watchdog indisponível: {}".format(exc))
        return None


def bootstrap():
    # Inicializações essenciais (tolerantes a falha)
    wifi.conectar()
    sincronizar_horario.sincronizar_ntp()
    sincronizar_horario.imprimir_hora_atual(prefixo="[BOOT]")

    registros_sdcard.inicializar_sdcard()
    registros_sdcard.garantir_estrutura_diretorios(["/logs", "/imagens"])

    clima.inicializar_sensor_hdc1080(sda=14, scl=21, freq=100000, endereco=0x40)
    luminosidade.inicializar_sensor_ldr(pin_adc=PIN_LDR_ADC, largura_bits=12)
    rega.inicializar_sensor_solo(pin_adc=PIN_SOLO_ADC)

    alarme_predadores.inicializar_pir(pin=PIN_PIR)
    alarme_predadores.inicializar_buzzer()

    rega.configurar_rele_bomba(pin=PIN_RELE_BOMBA, ativo_em_nivel_baixo=True, timeout_s=30)
    clima.configurar_rele_lampada(pin=PIN_RELE_LAMPADA, ativo_em_nivel_baixo=True, timeout_s=60)

    captura_de_imagem.inicializar_camera_ov5640()
    envio_e_recebimento_nuvem.inicializar_cliente_mqtt()


def ciclo_telemetria_e_automacao():
    leitura_clima = clima.ler_clima()
    leitura_solo = rega.ler_umidade_solo()
    leitura_ldr = luminosidade.ler_luminosidade()
    movimento = alarme_predadores.detectar_movimento()

    payload = {
        "timestamp": sincronizar_horario.agora_iso8601(),
        "clima": leitura_clima,
        "solo": leitura_solo,
        "luminosidade": leitura_ldr,
        "movimento": movimento,
        "device": {"mac": _mac_address(), "uid": _chip_info().get("uid_hex")},
    }

    # Automação local
    rega.avaliar_e_acionar_irrigacao(leitura_solo)
    clima.avaliar_e_acionar_aquecimento(leitura_clima)

    # Persistência e nuvem
    registros_sdcard.registrar_linha_csv("/logs/telemetria.csv", payload)
    envio_e_recebimento_nuvem.publicar_telemetria(payload)

    # Alertas de movimento
    if movimento:
        alarme_predadores.acionar_alarme_local()
        envio_e_recebimento_nuvem.publicar_alerta_movimento(payload)

    # Comandos remotos (rega/lâmpada/captura)
    envio_e_recebimento_nuvem.processar_comandos_pendentes(
        on_irrigar=rega.comando_irrigacao,
        on_aquecer=clima.comando_aquecimento,
        on_capturar=lambda: captura_de_imagem.capturar_e_salvar("/imagens"),
    )


def executar():
    print_boot_banner()
    # Evita reset por watchdog durante bootstrap de rede/cloud (pode bloquear).
    bootstrap()
    wdt = setup_watchdog()

    ultimo_time_lapse_dia = None

    while True:
        try:
            if wdt:
                wdt.feed()

            wifi.garantir_conectividade()
            ciclo_telemetria_e_automacao()

            # Captura diária de imagem (time-lapse)
            dia_atual = sincronizar_horario.dia_atual_str()
            if dia_atual and dia_atual != ultimo_time_lapse_dia:
                captura_de_imagem.capturar_e_salvar("/imagens")
                ultimo_time_lapse_dia = dia_atual

        except Exception as exc:
            print("[ERRO] loop principal:", exc)
            try:
                registros_sdcard.registrar_evento("/logs/eventos.log", "erro_loop", str(exc))
            except Exception:
                pass

        time.sleep_ms(LOOP_INTERVAL_MS)


if __name__ == "__main__":
    executar()
