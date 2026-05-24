"""
envio_e_recebimento_nuvem.py - Camada de integração em nuvem (MQTT)

Modo atual: broker público para validação rápida de comunicação bidirecional.
"""

import json
import time
from umqtt.simple import MQTTClient

import wifi

DEFAULT_DEVICE_ID = "esp32-estufa-dev"
DEFAULT_MQTT_BROKER = "broker.hivemq.com"
DEFAULT_MQTT_PORT = 1883
DEFAULT_MQTT_USER = ""
DEFAULT_MQTT_PASSWORD = ""
DEFAULT_MQTT_KEEPALIVE = 60
DEFAULT_MQTT_SSL = False
DEFAULT_MQTT_SSL_PARAMS = {}
DEFAULT_SOCKET_TIMEOUT_S = 8
DEFAULT_CA_CERT_PATH = "isrgrootx1.txt"


def _carregar_config_mqtt():
    cfg = {
        "DEVICE_ID": DEFAULT_DEVICE_ID,
        "MQTT_BROKER": DEFAULT_MQTT_BROKER,
        "MQTT_PORT": DEFAULT_MQTT_PORT,
        "MQTT_USER": DEFAULT_MQTT_USER,
        "MQTT_PASSWORD": DEFAULT_MQTT_PASSWORD,
        "MQTT_KEEPALIVE": DEFAULT_MQTT_KEEPALIVE,
        "MQTT_SSL": DEFAULT_MQTT_SSL,
        "MQTT_SSL_PARAMS": DEFAULT_MQTT_SSL_PARAMS,
        "SOCKET_TIMEOUT_S": DEFAULT_SOCKET_TIMEOUT_S,
        "CA_CERT_PATH": DEFAULT_CA_CERT_PATH,
    }
    try:
        secrets = __import__("secrets")
        cfg["DEVICE_ID"] = getattr(secrets, "MQTT_DEVICE_ID", cfg["DEVICE_ID"])
        cfg["MQTT_BROKER"] = getattr(secrets, "MQTT_BROKER", cfg["MQTT_BROKER"])
        cfg["MQTT_PORT"] = getattr(secrets, "MQTT_PORT", cfg["MQTT_PORT"])
        cfg["MQTT_USER"] = getattr(secrets, "MQTT_USER", cfg["MQTT_USER"])
        cfg["MQTT_PASSWORD"] = getattr(secrets, "MQTT_PASSWORD", cfg["MQTT_PASSWORD"])
        cfg["MQTT_KEEPALIVE"] = getattr(secrets, "MQTT_KEEPALIVE", cfg["MQTT_KEEPALIVE"])
        cfg["MQTT_SSL"] = getattr(secrets, "MQTT_SSL", cfg["MQTT_SSL"])
        cfg["MQTT_SSL_PARAMS"] = getattr(secrets, "MQTT_SSL_PARAMS", cfg["MQTT_SSL_PARAMS"])
        cfg["SOCKET_TIMEOUT_S"] = getattr(secrets, "MQTT_SOCKET_TIMEOUT_S", cfg["SOCKET_TIMEOUT_S"])
        cfg["CA_CERT_PATH"] = getattr(secrets, "MQTT_CA_CERT_PATH", cfg["CA_CERT_PATH"])
    except Exception:
        pass
    return cfg


_CFG = _carregar_config_mqtt()
DEVICE_ID = _CFG["DEVICE_ID"]
MQTT_BROKER = _CFG["MQTT_BROKER"]
MQTT_PORT = _CFG["MQTT_PORT"]
MQTT_USER = _CFG["MQTT_USER"]
MQTT_PASSWORD = _CFG["MQTT_PASSWORD"]
MQTT_KEEPALIVE = _CFG["MQTT_KEEPALIVE"]
MQTT_SSL = _CFG["MQTT_SSL"]
MQTT_SSL_PARAMS = _CFG["MQTT_SSL_PARAMS"]
SOCKET_TIMEOUT_S = _CFG["SOCKET_TIMEOUT_S"]
CA_CERT_PATH = _CFG["CA_CERT_PATH"]

TOPICO_BASE = "estufa/{}/".format(DEVICE_ID)
TOPICO_TELEMETRIA = TOPICO_BASE + "telemetria"
TOPICO_ALERTAS = TOPICO_BASE + "alertas"
TOPICO_STATUS = TOPICO_BASE + "status"
TOPICO_COMANDOS = TOPICO_BASE + "comandos"
TOPICO_TESTE = TOPICO_BASE + "teste"

_CLIENTE = None
_FILA_COMANDOS = []


def _carregar_ssl_params():
    params = MQTT_SSL_PARAMS if isinstance(MQTT_SSL_PARAMS, dict) else {}
    params = dict(params)
    if not MQTT_SSL:
        return params
    if "ca" in params:
        return params
    try:
        with open(CA_CERT_PATH, "r") as f:
            params["ca"] = f.read()
        print("[MQTT][TLS] CA carregada de {}".format(CA_CERT_PATH))
    except Exception as exc:
        print("[MQTT][TLS][WARN] Não foi possível carregar CA em '{}': {}".format(CA_CERT_PATH, exc))
    return params


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


def _criar_cliente_mqtt(user, password, ssl_params):
    tentativas = [
        ("posicional_com_ssl_params", (DEVICE_ID, MQTT_BROKER, MQTT_PORT, user, password, MQTT_KEEPALIVE, MQTT_SSL, ssl_params), {}),
        ("posicional_sem_ssl_params", (DEVICE_ID, MQTT_BROKER, MQTT_PORT, user, password, MQTT_KEEPALIVE, MQTT_SSL), {}),
        ("kwargs_sem_ssl_params", (), {
            "client_id": DEVICE_ID,
            "server": MQTT_BROKER,
            "port": MQTT_PORT,
            "user": user,
            "password": password,
            "keepalive": MQTT_KEEPALIVE,
            "ssl": MQTT_SSL,
        }),
        ("minimo", (DEVICE_ID, MQTT_BROKER), {}),
    ]

    ultimo_erro = None
    for nome, args, kwargs in tentativas:
        try:
            cliente = MQTTClient(*args, **kwargs)
            print("[MQTT][DIAG] construtor compatível: {}".format(nome))
            return cliente
        except TypeError as exc:
            ultimo_erro = exc
            print("[MQTT][WARN] assinatura não suportada ({}): {}".format(nome, exc))
        except Exception as exc:
            ultimo_erro = exc
            print("[MQTT][WARN] falha ao criar cliente ({}): {}".format(nome, exc))

    raise ultimo_erro if ultimo_erro else Exception("Falha desconhecida no construtor MQTTClient")


def validar_configuracao_mqtt():
    if MQTT_PORT == 8883 and not MQTT_SSL:
        print("[MQTT][ERRO] Porta 8883 requer TLS. Defina MQTT_SSL=True.")
        return False
    if not MQTT_BROKER or not DEVICE_ID:
        print("[MQTT][ERRO] MQTT_BROKER/DEVICE_ID não configurados.")
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
    ssl_params = _carregar_ssl_params()

    try:
        _CLIENTE = _criar_cliente_mqtt(user, password, ssl_params)
        _CLIENTE.set_callback(_on_mqtt_msg)
        try:
            _CLIENTE.sock.settimeout(SOCKET_TIMEOUT_S)
        except Exception:
            pass
        _CLIENTE.connect()
        _CLIENTE.subscribe(TOPICO_COMANDOS)
        _CLIENTE.publish(TOPICO_STATUS, _json_dump({"online": True, "ts_ms": time.ticks_ms(), "device_id": DEVICE_ID}), retain=True)
        _CLIENTE.publish(TOPICO_TESTE, _json_dump({"evento": "boot", "device_id": DEVICE_ID, "ts_ms": time.ticks_ms()}))
        imprimir_topicos()
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
        _CLIENTE.publish(TOPICO_ALERTAS, _json_dump({"tipo": "movimento", "payload": payload}))
        return True
    except Exception as exc:
        print("[MQTT][WARN] Falha ao publicar alerta: {}".format(exc))
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