#pragma once

// Copie este arquivo para firmware_arduino/config.h e ajuste valores locais.
// config.h esta no .gitignore e nao deve ser versionado.

// Identidade do dispositivo / MQTT
#define DEVICE_ID "esp32s3-estufa-001"
#define MQTT_NAMESPACE "embarcatech2026"
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""
#define MQTT_KEEPALIVE_SECONDS 60

// Wi-Fi
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// Firebase uploadCameraImage
#define CAMERA_UPLOAD_URL "https://us-central1-SEU_PROJETO.cloudfunctions.net/uploadCameraImage"
#define CAMERA_UPLOAD_TOKEN "troque-este-token-local"
#define CAMERA_CONTENT_TYPE "image/jpeg"

// NTP / horario local. Brasilia = UTC-3 sem horario de verao.
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SECONDS (-3 * 60 * 60)
#define DAYLIGHT_OFFSET_SECONDS 0

// GPIOs principais do projeto
#define PIN_RELE_BOMBA 47
#define PIN_RELE_LAMPADA 48
#define PIN_PIR 42
#define PIN_SOLO_ADC 41
#define PIN_LDR_ADC 1
#define PIN_VENTOINHA 44
// Botao local opcional para testar captura/upload sem depender do app/MQTT.
// GPIO45 e pino de strapping no ESP32-S3: use botao para GND com INPUT_PULLUP
// e nao mantenha pressionado durante boot/reset.
#define PIN_BOTAO_CAMERA 45
#define CAMERA_BUTTON_ENABLED true
#define CAMERA_BUTTON_ACTIVE_LOW true
#define CAMERA_BUTTON_DEBOUNCE_MS 80UL
#define CAMERA_BUTTON_COOLDOWN_MS 5000UL
#define RELAY_ACTIVE_LOW true

// Periodicidade basica de telemetria
#define TELEMETRY_INTERVAL_MS 60000UL

// Configuracao padrao da rotina de camera
#define CAMERA_AUTO_CAPTURE_ENABLED true
#define CAMERA_CAPTURE_HOUR 12
#define CAMERA_CAPTURE_MINUTE 0
#define CAMERA_CAPTURE_INTERVAL_HOURS 24

// OV5640 / esp32-camera
// IMPORTANTE: substitua pelos pinos reais da sua placa ESP32-S3 + OV5640.
// Os valores abaixo sao placeholders e provavelmente nao funcionarao sem ajuste.
#define CAMERA_PIN_PWDN -1
#define CAMERA_PIN_RESET -1
#define CAMERA_PIN_XCLK -1
#define CAMERA_PIN_SIOD -1
#define CAMERA_PIN_SIOC -1
#define CAMERA_PIN_D0 -1
#define CAMERA_PIN_D1 -1
#define CAMERA_PIN_D2 -1
#define CAMERA_PIN_D3 -1
#define CAMERA_PIN_D4 -1
#define CAMERA_PIN_D5 -1
#define CAMERA_PIN_D6 -1
#define CAMERA_PIN_D7 -1
#define CAMERA_PIN_VSYNC -1
#define CAMERA_PIN_HREF -1
#define CAMERA_PIN_PCLK -1

#define CAMERA_XCLK_FREQ_HZ 20000000
#define CAMERA_FRAME_SIZE FRAMESIZE_SVGA
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 1
#define CAMERA_GRAB_MODE CAMERA_GRAB_WHEN_EMPTY
#define CAMERA_USE_PSRAM_FRAMEBUFFER true
#define CAMERA_COPY_FRAME_BEFORE_UPLOAD true
#define CAMERA_DEINIT_BEFORE_UPLOAD true
#define CAMERA_HTTP_TIMEOUT_MS 20000
#define CAMERA_CAPTURE_RETRY_COUNT 3
#define CAMERA_CAPTURE_RETRY_DELAY_MS 700UL
