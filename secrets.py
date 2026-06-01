"""
Copie este arquivo para `secrets.py` e preencha conforme seu ambiente local.
`secrets.py` está no .gitignore e não deve ser versionado.
"""

# Wi-Fi
WIFI_SSID = "Alencar's Galaxy M14 5G"
WIFI_PASSWORD = "11223344"

# MQTT (modo prototipação / broker público)
MQTT_DEVICE_ID = "esp32s3-estufa-001"
MQTT_NAMESPACE = "embarcatech2026"
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_USER = ""
MQTT_PASSWORD = ""
MQTT_KEEPALIVE = 60

# Timeout de operações de socket MQTT (segundos)
MQTT_SOCKET_TIMEOUT_S = 8

# Câmera OV5640
# Ajuste os pinos conforme o kit ESP32-S3 + OV5640 usado.
# Exemplo: placas diferentes expõem nomes/pinos distintos no firmware camera.
CAMERA_PINS = {
    # "pin_pwdn": -1,
    # "pin_reset": -1,
    # "pin_xclk": 15,
    # "pin_siod": 4,
    # "pin_sioc": 5,
    # "pin_d0": 11,
    # "pin_d1": 9,
    # "pin_d2": 8,
    # "pin_d3": 10,
    # "pin_d4": 12,
    # "pin_d5": 18,
    # "pin_d6": 17,
    # "pin_d7": 16,
    # "pin_vsync": 6,
    # "pin_href": 7,
    # "pin_pclk": 13,
}
CAMERA_LOCAL_DIR = "/imagens"
CAMERA_CONFIG_PATH = "/camera_config.json"
CAMERA_FRAME_SIZE = "SVGA"
CAMERA_JPEG_QUALITY = 12
CAMERA_CONTENT_TYPE = "image/jpeg"

# Captura automática: padrão inicial, uma vez por dia às 12h00.
# Se CAMERA_CAPTURE_INTERVAL_HOURS for menor que 24, captura em janelas periódicas
# a partir de CAMERA_CAPTURE_HOUR e no minuto CAMERA_CAPTURE_MINUTE.
CAMERA_AUTO_CAPTURE_ENABLED = True
CAMERA_CAPTURE_HOUR = 12
CAMERA_CAPTURE_MINUTE = 0
CAMERA_CAPTURE_INTERVAL_HOURS = 24

# Endpoint HTTP da Cloud Function uploadCameraImage.
# Exemplo após deploy:
# CAMERA_UPLOAD_URL = "https://us-central1-SEU_PROJETO.cloudfunctions.net/uploadCameraImage"
CAMERA_UPLOAD_URL = ""
CAMERA_UPLOAD_TOKEN = "estufa-iot-cam-2026-X7r9pQ2mL8vA4sT1"
