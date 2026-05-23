"""
envio_e_recebimento_nuvem.py - Camada de integração em nuvem (MQTT)
"""

import json
import time
from umqtt.simple import MQTTClient

import wifi

DEFAULT_DEVICE_ID = "esp32s3-estufa-001"
DEFAULT_MQTT_BROKER = "0754d4c62cd348ccaf698cc85aea935b.s1.eu.hivemq.cloud"
DEFAULT_MQTT_PORT = 8883
DEFAULT_MQTT_USER = "estufa_iot"
DEFAULT_MQTT_PASSWORD = "Naodigo2026"
DEFAULT_MQTT_KEEPALIVE = 60
DEFAULT_MQTT_SSL = True
DEFAULT_MQTT_SSL_PARAMS = {}
DEFAULT_SOCKET_TIMEOUT_S = 8
DEFAULT_CA_CERT_PATH = "/certs/isrgrootx1.pem"



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
            ca_pem = f.read()
        params["ca"] = ca_pem
        print("[MQTT][TLS] CA carregada de {}".format(CA_CERT_PATH))
    except Exception as exc:
        print("[MQTT][TLS][WARN] Não foi possível carregar CA em '{}': {}".format(CA_CERT_PATH, exc))
        print("[MQTT][TLS][WARN] Para HiveMQ Cloud, configure MQTT_CA_CERT_PATH ou MQTT_SSL_PARAMS={'ca': '...PEM...'} em secrets.py.")

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




def validar_configuracao_mqtt():
    if MQTT_PORT == 8883 and not MQTT_SSL:
        print("[MQTT][ERRO] Porta 8883 requer TLS. Defina MQTT_SSL=True.")
        return False
    if MQTT_PORT == 1883 and MQTT_SSL:
        print("[MQTT][WARN] Porta 1883 normalmente é sem TLS. Revise MQTT_SSL.")
    if MQTT_PORT == 8884:
        print("[MQTT][WARN] Porta 8884 costuma ser MQTT over WebSocket TLS no HiveMQ. Para ESP32 + umqtt.simple use 8883.")
    if MQTT_SSL and not CA_CERT_PATH and (not isinstance(MQTT_SSL_PARAMS, dict) or "ca" not in MQTT_SSL_PARAMS):
        print("[MQTT][WARN] TLS ativo sem CA configurada. HiveMQ pode recusar handshake.")
    if not MQTT_BROKER:
        print("[MQTT][ERRO] MQTT_BROKER não configurado.")
        return False
    if not DEVICE_ID:
        print("[MQTT][ERRO] DEVICE_ID não configurado.")
        return False
    return True



def diagnostico_pre_conexao():
    print("[MQTT][DIAG] broker={} porta={} ssl={}".format(MQTT_BROKER, MQTT_PORT, MQTT_SSL))
    print("[MQTT][DIAG] user_configurado={} password_configurada={} device_id={}".format(
        bool(MQTT_USER), bool(MQTT_PASSWORD), DEVICE_ID
    ))
    if MQTT_USER == DEVICE_ID:
        print("[MQTT][DIAG][WARN] Username igual ao device_id. No HiveMQ Cloud, use a credencial criada em Access Management.")
    if MQTT_PORT == 8883 and not MQTT_SSL:
        print("[MQTT][DIAG][ERRO] Porta 8883 exige TLS (MQTT_SSL=True).")

def inicializar_cliente_mqtt():
    global _CLIENTE

    if not validar_configuracao_mqtt():
        return False

    diagnostico_pre_conexao()

    if not wifi.conectado():
        print("[MQTT][WARN] Wi-Fi indisponível para iniciar MQTT.")
        return False

    if _CLIENTE is not None:
        return True

    user = MQTT_USER or None
    password = MQTT_PASSWORD or None

    # ssl_params = _carregar_ssl_params()
    ssl_params = {}

    try:
        try:
            # Prioriza forma posicional para compatibilidade com variações de assinatura do umqtt.simple
            _CLIENTE = MQTTClient(
                DEVICE_ID,
                MQTT_BROKER,
                MQTT_PORT,
                user,
                password,
                MQTT_KEEPALIVE,
                MQTT_SSL,
                ssl_params,
            )
        except TypeError as exc_ctor:
            if "extra keyword arguments given" in str(exc_ctor):
                print("[MQTT][WARN] Build de umqtt.simple não aceita ssl_params; tentando construtor compatível.")
                _CLIENTE = MQTTClient(
                    DEVICE_ID,
                    MQTT_BROKER,
                    MQTT_PORT,
                    user,
                    password,
                    MQTT_KEEPALIVE,
                    MQTT_SSL,
                )
            else:
                raise
        _CLIENTE.set_callback(_on_mqtt_msg)
        try:
            _CLIENTE.sock.settimeout(SOCKET_TIMEOUT_S)
        except Exception:
            pass
        _CLIENTE.connect()
        _CLIENTE.subscribe(TOPICO_COMANDOS)

        publicar_status({"online": True, "ts_ms": time.ticks_ms(), "device_id": DEVICE_ID})
        publicar_teste_boot()
        imprimir_topicos()
        print("[MQTT] Conectado com sucesso e assinando {}".format(TOPICO_COMANDOS))
        return True
    except Exception as exc:
        print("[MQTT][ERRO] Falha ao inicializar cliente: {}".format(exc))
        _explicar_erro_conexao(exc)
        _CLIENTE = None
        return False




def _explicar_erro_conexao(exc):
    codigo = None
    if hasattr(exc, "args") and exc.args:
        codigo = exc.args[0]

    mapa = {
        1: "CONNACK 1: versão de protocolo MQTT não aceita pelo broker.",
        2: "CONNACK 2: client_id rejeitado.",
        3: "CONNACK 3: servidor MQTT indisponível.",
        4: "CONNACK 4: usuário/senha inválidos.",
        5: "CONNACK 5: não autorizado (credencial/ACL).",
    }

    if codigo in mapa:
        print("[MQTT][ERRO][DIAG] {}".format(mapa[codigo]))
        if codigo in (4, 5):
            print("[MQTT][ERRO][DIAG] Verifique Credentials e ACL no HiveMQ Cloud para o usuário configurado.")
    else:
        print("[MQTT][ERRO][DIAG] Erro não mapeado: {}".format(exc))

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
