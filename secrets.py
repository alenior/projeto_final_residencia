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
