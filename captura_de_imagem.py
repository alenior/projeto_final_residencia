"""
captura_de_imagem.py - Rotina de camera OV5640 para ESP32-S3 (MicroPython)

Responsabilidades:
- Inicializar a camera de forma isolada do `main.py`.
- Capturar imagens manuais e automaticas.
- Controlar agenda configuravel por comando MQTT/app.
- Enviar a imagem para uma Cloud Function HTTP que grava no Firebase Storage.

Observacao de hardware:
- O suporte MicroPython para cameras depende do firmware/port usado na placa.
- Ajuste pinos, frame size e qualidade no `secrets.py` conforme o modulo OV5640
  e o kit ESP32-S3 utilizado.
"""

import json
import os
import time

DEFAULT_DEVICE_ID = "esp32-estufa-dev"
DEFAULT_NAMESPACE = "embarcatech2026"
DEFAULT_LOCAL_DIR = "/imagens"
DEFAULT_AUTO_ENABLED = True
DEFAULT_CAPTURE_HOUR = 12
DEFAULT_CAPTURE_MINUTE = 0
DEFAULT_INTERVAL_HOURS = 24
DEFAULT_UPLOAD_URL = ""
DEFAULT_UPLOAD_TOKEN = ""
DEFAULT_CONFIG_PATH = "/camera_config.json"
DEFAULT_CONTENT_TYPE = "image/jpeg"
DEFAULT_FRAME_SIZE = "SVGA"
DEFAULT_JPEG_QUALITY = 12
DEFAULT_CAMERA_PINS = {}

_CAMERA = None
_CAMERA_INICIALIZADA = False
_ULTIMA_CAPTURA_CHAVE = None


def _carregar_config_camera():
    cfg = {
        "DEVICE_ID": DEFAULT_DEVICE_ID,
        "NAMESPACE": DEFAULT_NAMESPACE,
        "LOCAL_DIR": DEFAULT_LOCAL_DIR,
        "AUTO_ENABLED": DEFAULT_AUTO_ENABLED,
        "CAPTURE_HOUR": DEFAULT_CAPTURE_HOUR,
        "CAPTURE_MINUTE": DEFAULT_CAPTURE_MINUTE,
        "INTERVAL_HOURS": DEFAULT_INTERVAL_HOURS,
        "UPLOAD_URL": DEFAULT_UPLOAD_URL,
        "UPLOAD_TOKEN": DEFAULT_UPLOAD_TOKEN,
        "CONFIG_PATH": DEFAULT_CONFIG_PATH,
        "CONTENT_TYPE": DEFAULT_CONTENT_TYPE,
        "FRAME_SIZE": DEFAULT_FRAME_SIZE,
        "JPEG_QUALITY": DEFAULT_JPEG_QUALITY,
        "CAMERA_PINS": DEFAULT_CAMERA_PINS,
    }
    try:
        secrets = __import__("secrets")
        cfg["DEVICE_ID"] = getattr(secrets, "MQTT_DEVICE_ID", cfg["DEVICE_ID"])
        cfg["NAMESPACE"] = getattr(secrets, "MQTT_NAMESPACE", cfg["NAMESPACE"])
        cfg["LOCAL_DIR"] = getattr(secrets, "CAMERA_LOCAL_DIR", cfg["LOCAL_DIR"])
        cfg["AUTO_ENABLED"] = getattr(secrets, "CAMERA_AUTO_CAPTURE_ENABLED", cfg["AUTO_ENABLED"])
        cfg["CAPTURE_HOUR"] = getattr(secrets, "CAMERA_CAPTURE_HOUR", cfg["CAPTURE_HOUR"])
        cfg["CAPTURE_MINUTE"] = getattr(secrets, "CAMERA_CAPTURE_MINUTE", cfg["CAPTURE_MINUTE"])
        cfg["INTERVAL_HOURS"] = getattr(secrets, "CAMERA_CAPTURE_INTERVAL_HOURS", cfg["INTERVAL_HOURS"])
        cfg["UPLOAD_URL"] = getattr(secrets, "CAMERA_UPLOAD_URL", cfg["UPLOAD_URL"])
        cfg["UPLOAD_TOKEN"] = getattr(secrets, "CAMERA_UPLOAD_TOKEN", cfg["UPLOAD_TOKEN"])
        cfg["CONFIG_PATH"] = getattr(secrets, "CAMERA_CONFIG_PATH", cfg["CONFIG_PATH"])
        cfg["CONTENT_TYPE"] = getattr(secrets, "CAMERA_CONTENT_TYPE", cfg["CONTENT_TYPE"])
        cfg["FRAME_SIZE"] = getattr(secrets, "CAMERA_FRAME_SIZE", cfg["FRAME_SIZE"])
        cfg["JPEG_QUALITY"] = getattr(secrets, "CAMERA_JPEG_QUALITY", cfg["JPEG_QUALITY"])
        cfg["CAMERA_PINS"] = getattr(secrets, "CAMERA_PINS", cfg["CAMERA_PINS"])
    except Exception:
        pass

    try:
        with open(cfg["CONFIG_PATH"], "r") as arquivo:
            persistida = json.loads(arquivo.read())
        for chave in ("AUTO_ENABLED", "CAPTURE_HOUR", "CAPTURE_MINUTE", "INTERVAL_HOURS"):
            if chave in persistida:
                cfg[chave] = persistida[chave]
    except Exception:
        pass

    return cfg


_CONFIG = _carregar_config_camera()


def _bool(valor):
    if isinstance(valor, str):
        return valor.lower() in ("1", "true", "sim", "yes", "on")
    return bool(valor)


def _int_limitado(valor, padrao, minimo, maximo):
    try:
        valor = int(valor)
    except Exception:
        valor = padrao
    if valor < minimo:
        return minimo
    if valor > maximo:
        return maximo
    return valor



def _persistir_config_camera():
    dados = {
        "AUTO_ENABLED": _CONFIG.get("AUTO_ENABLED"),
        "CAPTURE_HOUR": _CONFIG.get("CAPTURE_HOUR"),
        "CAPTURE_MINUTE": _CONFIG.get("CAPTURE_MINUTE"),
        "INTERVAL_HOURS": _CONFIG.get("INTERVAL_HOURS"),
    }
    try:
        with open(_CONFIG.get("CONFIG_PATH") or DEFAULT_CONFIG_PATH, "w") as arquivo:
            arquivo.write(json.dumps(dados))
    except Exception as exc:
        print("[CAMERA][WARN] Nao foi possivel persistir agenda: {}".format(exc))


def _timestamp_partes():
    agora = time.localtime()
    return {
        "ano": agora[0],
        "mes": agora[1],
        "dia": agora[2],
        "hora": agora[3],
        "minuto": agora[4],
        "segundo": agora[5],
    }


def _timestamp_iso8601():
    partes = _timestamp_partes()
    return "{ano:04d}-{mes:02d}-{dia:02d}T{hora:02d}:{minuto:02d}:{segundo:02d}".format(**partes)


def _nome_arquivo(prefixo="ov5640"):
    partes = _timestamp_partes()
    return "{prefixo}_{ano:04d}{mes:02d}{dia:02d}_{hora:02d}{minuto:02d}{segundo:02d}.jpg".format(
        prefixo=prefixo,
        **partes
    )


def _garantir_diretorio(diretorio):
    try:
        os.mkdir(diretorio)
    except OSError:
        pass


def _camera_constante(camera_modulo, nome, padrao=None):
    return getattr(camera_modulo, nome, padrao)


def _resolver_frame_size(camera_modulo, valor):
    if not isinstance(valor, str):
        return valor
    candidatos = (valor, "FRAME_" + valor, "FRAMESIZE_" + valor)
    for nome in candidatos:
        constante = _camera_constante(camera_modulo, nome, None)
        if constante is not None:
            return constante
    return valor


def inicializar_camera_ov5640(sda=None, scl=None, freq=None, endereco=None):
    """Inicializa a camera OV5640, quando o firmware MicroPython oferecer modulo camera."""
    global _CAMERA, _CAMERA_INICIALIZADA
    if _CAMERA_INICIALIZADA:
        return True

    try:
        camera_modulo = __import__("camera")
    except Exception as exc:
        print("[CAMERA][WARN] Modulo camera indisponivel neste firmware: {}".format(exc))
        _CAMERA = None
        _CAMERA_INICIALIZADA = False
        return False

    try:
        pins = _CONFIG.get("CAMERA_PINS") or {}
        if hasattr(camera_modulo, "deinit"):
            try:
                camera_modulo.deinit()
            except Exception:
                pass

        init_kwargs = {}
        for chave, valor in pins.items():
            init_kwargs[chave] = valor

        try:
            camera_modulo.init(0, **init_kwargs)
        except TypeError:
            camera_modulo.init(0)

        frame_size = _resolver_frame_size(camera_modulo, _CONFIG.get("FRAME_SIZE"))
        if hasattr(camera_modulo, "framesize"):
            camera_modulo.framesize(frame_size)
        if hasattr(camera_modulo, "quality"):
            camera_modulo.quality(_CONFIG.get("JPEG_QUALITY"))

        _CAMERA = camera_modulo
        _CAMERA_INICIALIZADA = True
        print("[CAMERA] OV5640 inicializada. frame_size={} qualidade={}".format(
            _CONFIG.get("FRAME_SIZE"),
            _CONFIG.get("JPEG_QUALITY"),
        ))
        imprimir_configuracao_camera()
        return True
    except Exception as exc:
        print("[CAMERA][ERRO] Falha ao inicializar OV5640: {}".format(exc))
        _CAMERA = None
        _CAMERA_INICIALIZADA = False
        return False


def imprimir_configuracao_camera():
    print("[CAMERA] auto={} horario={:02d}:{:02d} periodicidade={}h upload={}".format(
        _CONFIG.get("AUTO_ENABLED"),
        int(_CONFIG.get("CAPTURE_HOUR")),
        int(_CONFIG.get("CAPTURE_MINUTE")),
        int(_CONFIG.get("INTERVAL_HOURS")),
        "configurado" if _CONFIG.get("UPLOAD_URL") else "nao_configurado",
    ))


def configurar_agendamento(cmd):
    """Atualiza a agenda a partir de um comando MQTT vindo do app/Firebase."""
    if cmd is None:
        cmd = {}

    if "habilitado" in cmd:
        _CONFIG["AUTO_ENABLED"] = _bool(cmd.get("habilitado"))
    elif "enabled" in cmd:
        _CONFIG["AUTO_ENABLED"] = _bool(cmd.get("enabled"))
    elif "status" in cmd:
        _CONFIG["AUTO_ENABLED"] = _bool(cmd.get("status"))

    hora = cmd.get("hora", cmd.get("hour", cmd.get("capture_hour", _CONFIG.get("CAPTURE_HOUR"))))
    minuto = cmd.get("minuto", cmd.get("minute", cmd.get("capture_minute", _CONFIG.get("CAPTURE_MINUTE"))))
    intervalo = cmd.get("periodicidade_horas", cmd.get("interval_hours", cmd.get("capture_interval_hours", _CONFIG.get("INTERVAL_HOURS"))))

    _CONFIG["CAPTURE_HOUR"] = _int_limitado(hora, DEFAULT_CAPTURE_HOUR, 0, 23)
    _CONFIG["CAPTURE_MINUTE"] = _int_limitado(minuto, DEFAULT_CAPTURE_MINUTE, 0, 59)
    _CONFIG["INTERVAL_HOURS"] = _int_limitado(intervalo, DEFAULT_INTERVAL_HOURS, 1, 168)

    _persistir_config_camera()
    print("[CAMERA][CFG] Agenda atualizada por comando: {}".format(cmd))
    imprimir_configuracao_camera()
    return True


def _chave_agendamento_atual():
    partes = _timestamp_partes()
    return "{ano:04d}-{mes:02d}-{dia:02d}-{hora:02d}-{minuto:02d}".format(**partes)


def captura_automatica_pendente():
    """Retorna True uma vez por janela de agenda configurada."""
    global _ULTIMA_CAPTURA_CHAVE
    if not _CONFIG.get("AUTO_ENABLED"):
        return False

    partes = _timestamp_partes()
    hora_inicial = int(_CONFIG.get("CAPTURE_HOUR"))
    minuto_alvo = int(_CONFIG.get("CAPTURE_MINUTE"))
    intervalo = int(_CONFIG.get("INTERVAL_HOURS"))

    if partes["minuto"] != minuto_alvo:
        return False

    if intervalo >= 24:
        if partes["hora"] != hora_inicial:
            return False
    else:
        diferenca = (partes["hora"] - hora_inicial) % 24
        if diferenca % intervalo != 0:
            return False

    chave = _chave_agendamento_atual()
    if chave == _ULTIMA_CAPTURA_CHAVE:
        return False

    _ULTIMA_CAPTURA_CHAVE = chave
    return True


def _capturar_bytes():
    if _CAMERA is None and not inicializar_camera_ov5640():
        return None

    try:
        if hasattr(_CAMERA, "capture"):
            return _CAMERA.capture()
        if hasattr(_CAMERA, "capture_jpg"):
            return _CAMERA.capture_jpg()
    except Exception as exc:
        print("[CAMERA][ERRO] Falha na captura: {}".format(exc))
        return None

    print("[CAMERA][WARN] API de captura nao encontrada no modulo camera.")
    return None


def _salvar_bytes(caminho, dados):
    with open(caminho, "wb") as arquivo:
        arquivo.write(dados)


def _base64_sem_quebra(dados):
    ubinascii = __import__("ubinascii")
    return ubinascii.b2a_base64(dados).decode().strip()


def _post_json(url, payload, token):
    urequests = __import__("urequests")
    headers = {"Content-Type": "application/json"}
    if token:
        headers["x-camera-upload-token"] = token
    resposta = urequests.post(url, data=json.dumps(payload), headers=headers)
    try:
        status_code = getattr(resposta, "status_code", None)
        texto = getattr(resposta, "text", "")
        return status_code, texto
    finally:
        try:
            resposta.close()
        except Exception:
            pass


def enviar_imagem_para_firebase(caminho, filename, motivo="manual", metadados=None):
    """Envia imagem para a Cloud Function `uploadCameraImage`, se configurada."""
    if not _CONFIG.get("UPLOAD_URL"):
        print("[CAMERA][UPLOAD] CAMERA_UPLOAD_URL ausente; imagem mantida localmente.")
        return {"ok": False, "skipped": True, "reason": "upload_url_ausente"}

    try:
        with open(caminho, "rb") as arquivo:
            dados = arquivo.read()

        payload = {
            "namespace": _CONFIG.get("NAMESPACE"),
            "deviceId": _CONFIG.get("DEVICE_ID"),
            "filename": filename,
            "contentType": _CONFIG.get("CONTENT_TYPE"),
            "imageBase64": _base64_sem_quebra(dados),
            "metadata": metadados or {},
            "reason": motivo,
            "capturedAt": _timestamp_iso8601(),
        }
        status_code, texto = _post_json(_CONFIG.get("UPLOAD_URL"), payload, _CONFIG.get("UPLOAD_TOKEN"))
        ok = status_code is not None and 200 <= int(status_code) < 300
        print("[CAMERA][UPLOAD] status={} ok={} resposta={}".format(status_code, ok, texto[:120]))
        return {"ok": ok, "status_code": status_code, "response": texto}
    except Exception as exc:
        print("[CAMERA][UPLOAD][ERRO] Falha ao enviar imagem: {}".format(exc))
        return {"ok": False, "error": str(exc)}


def capturar_e_salvar(diretorio=None, motivo="manual", metadados=None):
    """Captura uma imagem, salva localmente e tenta enviar ao Firebase."""
    diretorio = diretorio or _CONFIG.get("LOCAL_DIR") or DEFAULT_LOCAL_DIR
    _garantir_diretorio(diretorio)

    dados = _capturar_bytes()
    if not dados:
        return {"ok": False, "erro": "camera_indisponivel", "motivo": motivo}

    filename = _nome_arquivo()
    caminho = "{}/{}".format(diretorio.rstrip("/"), filename)
    try:
        _salvar_bytes(caminho, dados)
    except Exception as exc:
        print("[CAMERA][ERRO] Falha ao salvar {}: {}".format(caminho, exc))
        return {"ok": False, "erro": str(exc), "motivo": motivo}

    resultado = {
        "ok": True,
        "filename": filename,
        "path": caminho,
        "bytes": len(dados),
        "motivo": motivo,
        "captured_at": _timestamp_iso8601(),
    }
    print("[CAMERA] Imagem capturada: {} ({} bytes, motivo={})".format(caminho, len(dados), motivo))
    resultado["upload"] = enviar_imagem_para_firebase(caminho, filename, motivo=motivo, metadados=metadados)
    return resultado
