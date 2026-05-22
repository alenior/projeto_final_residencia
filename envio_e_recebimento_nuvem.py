"""
envio_e_recebimento_nuvem.py - Camada de integração em nuvem (MQTT)
"""

import json
import time
from umqtt.simple import MQTTClient

import wifi

DEVICE_ID = "esp32s3-estufa-001"
MQTT_BROKER = "0754d4c62cd348ccaf698cc85aea935b.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "esp32s3-estufa-001"
MQTT_PASSWORD = "Naodigo2026"
MQTT_KEEPALIVE = 60
MQTT_SSL = True
MQTT_SSL_PARAMS = {}

TOPICO_BASE = "estufa/{}/".format(DEVICE_ID)
TOPICO_TELEMETRIA = TOPICO_BASE + "telemetria"
TOPICO_ALERTAS = TOPICO_BASE + "alertas"
TOPICO_STATUS = TOPICO_BASE + "status"
TOPICO_COMANDOS = TOPICO_BASE + "comandos"
TOPICO_TESTE = TOPICO_BASE + "teste"

_CLIENTE = None
_FILA_COMANDOS = []


def _json_dump(payload):
    try:
        return json.dumps(payload)
    except Exception:
        return "{}"


def _parse_payload(raw_msg):
    try:
        if isinstance(raw_msg, bytes):
            raw_msg = raw_msg.decode("utf-8")
        return json.loads(raw_msg)
    except Exception:
        return None


def _on_mqtt_msg(topic, msg):
    topico = topic.decode() if isinstance(topic, bytes) else str(topic)
    payload = _parse_payload(msg)
    if payload is None:
        print("[MQTT][WARN] Payload inválido em {}: {}".format(topico, msg))
        return

    print("[MQTT][RX] {} => {}".format(topico, payload))
    _FILA_COMANDOS.append(payload)


def imprimir_topicos():
    print("[MQTT] broker={}:{} ssl={} keepalive={}".format(MQTT_BROKER, MQTT_PORT, MQTT_SSL, MQTT_KEEPALIVE))
    print("[MQTT] topicos: telemetria={} alertas={} status={} comandos={} teste={}".format(
        TOPICO_TELEMETRIA, TOPICO_ALERTAS, TOPICO_STATUS, TOPICO_COMANDOS, TOPICO_TESTE
    ))




def validar_configuracao_mqtt():
    if MQTT_PORT == 8883 and not MQTT_SSL:
        print("[MQTT][ERRO] Porta 8883 requer TLS. Defina MQTT_SSL=True.")
        return False
    if MQTT_PORT == 1883 and MQTT_SSL:
        print("[MQTT][WARN] Porta 1883 normalmente é sem TLS. Revise MQTT_SSL.")
    if not MQTT_BROKER:
        print("[MQTT][ERRO] MQTT_BROKER não configurado.")
        return False
    if not DEVICE_ID:
        print("[MQTT][ERRO] DEVICE_ID não configurado.")
        return False
    return True

def inicializar_cliente_mqtt():
    global _CLIENTE

    if not validar_configuracao_mqtt():
        return False

    if not wifi.conectado():
        print("[MQTT][WARN] Wi-Fi indisponível para iniciar MQTT.")
        return False

    if _CLIENTE is not None:
        return True

    user = MQTT_USER or None
    password = MQTT_PASSWORD or None

    try:
        _CLIENTE = MQTTClient(
            client_id=DEVICE_ID,
            server=MQTT_BROKER,
            port=MQTT_PORT,
            user=user,
            password=password,
            keepalive=MQTT_KEEPALIVE,
            ssl=MQTT_SSL,
            ssl_params=MQTT_SSL_PARAMS,
        )
        _CLIENTE.set_callback(_on_mqtt_msg)
        _CLIENTE.connect()
        _CLIENTE.subscribe(TOPICO_COMANDOS)

        publicar_status({"online": True, "ts_ms": time.ticks_ms(), "device_id": DEVICE_ID})
        publicar_teste_boot()
        imprimir_topicos()
        print("[MQTT] Conectado com sucesso e assinando {}".format(TOPICO_COMANDOS))
        return True
    except Exception as exc:
        print("[MQTT][ERRO] Falha ao inicializar cliente: {}".format(exc))
        _CLIENTE = None
        return False


def _garantir_cliente():
    if _CLIENTE is None:
        return inicializar_cliente_mqtt()
    return True


def publicar_telemetria(payload):
    if not _garantir_cliente():
        return False
    try:
        _CLIENTE.publish(TOPICO_TELEMETRIA, _json_dump(payload))
        return True
    except Exception as exc:
        print("[MQTT][WARN] Falha ao publicar telemetria: {}".format(exc))
        return False


def publicar_alerta_movimento(payload):
    if not _garantir_cliente():
        return False
    try:
        evento = {"tipo": "movimento", "payload": payload}
        _CLIENTE.publish(TOPICO_ALERTAS, _json_dump(evento))
        return True
    except Exception as exc:
        print("[MQTT][WARN] Falha ao publicar alerta: {}".format(exc))
        return False


def publicar_status(payload):
    if not _garantir_cliente():
        return False
    try:
        _CLIENTE.publish(TOPICO_STATUS, _json_dump(payload), retain=True)
        return True
    except Exception as exc:
        print("[MQTT][WARN] Falha ao publicar status: {}".format(exc))
        return False


def publicar_teste_boot():
    if not _garantir_cliente():
        return False
    payload = {
        "evento": "boot",
        "device_id": DEVICE_ID,
        "uptime_ms": time.ticks_ms(),
    }
    try:
        _CLIENTE.publish(TOPICO_TESTE, _json_dump(payload))
        print("[MQTT][TESTE] Publicado evento de boot em {}".format(TOPICO_TESTE))
        return True
    except Exception as exc:
        print("[MQTT][WARN] Falha no publish de teste: {}".format(exc))
        return False


def processar_comandos_pendentes(on_irrigar=None, on_aquecer=None, on_capturar=None):
    if _CLIENTE is not None:
        try:
            _CLIENTE.check_msg()
        except Exception as exc:
            print("[MQTT][WARN] check_msg falhou: {}".format(exc))

    while _FILA_COMANDOS:
        cmd = _FILA_COMANDOS.pop(0)
        comando = cmd.get("comando")
        status = cmd.get("status", True)

        try:
            if comando == "irrigar" and on_irrigar:
                on_irrigar(status)
            elif comando == "aquecer" and on_aquecer:
                on_aquecer(status)
            elif comando == "capturar" and on_capturar and status:
                on_capturar()
            else:
                print("[MQTT][WARN] Comando não tratado: {}".format(cmd))
        except Exception as exc:
            print("[MQTT][ERRO] Falha ao processar comando {}: {}".format(cmd, exc))
