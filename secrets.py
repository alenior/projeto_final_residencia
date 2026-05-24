"""
Copie este arquivo para `secrets.py` e preencha com credenciais reais.
`secrets.py` está no .gitignore e não deve ser versionado.
"""
# Wi-Fi
WIFI_SSID = "GCNET-Alencar"
WIFI_PASSWORD = "11223344"
# MQTT / HiveMQ Cloud
MQTT_DEVICE_ID = "esp32s3-estufa-001"
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_USER = ""
MQTT_PASSWORD = ""
MQTT_KEEPALIVE = 60
MQTT_SSL = False
MQTT_SSL_PARAMS = {}
# Timeout de operações de socket MQTT (segundos)
MQTT_SOCKET_TIMEOUT_S = 8
# Caminho para CA raiz PEM (ex.: ISRG Root X1 / Let's Encrypt)
MQTT_CA_CERT_PATH = "isrgrootx1.txt"
