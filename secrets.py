"""
Copie este arquivo para `secrets.py` e preencha com credenciais reais.
`secrets.py` está no .gitignore e não deve ser versionado.
"""

# Wi-Fi
WIFI_SSID = "Wokwi-GUEST"
WIFI_PASSWORD = ""

# MQTT / HiveMQ Cloud
MQTT_DEVICE_ID = "esp32s3-estufa-001"
MQTT_BROKER = "0754d4c62cd348ccaf698cc85aea935b.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "estufa_iot"
MQTT_PASSWORD = "Naodigo2026"
MQTT_KEEPALIVE = 60
MQTT_SSL = True
MQTT_SSL_PARAMS = {}

# Caminho para CA raiz PEM (ex.: ISRG Root X1 / Let's Encrypt)
MQTT_CA_CERT_PATH = "certs/isrgrootx1.pem"
