#include "camera_manager.h"
#include "config.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_camera.h>

#ifndef CAMERA_CAPTURE_RETRY_COUNT
#define CAMERA_CAPTURE_RETRY_COUNT 3
#endif
#ifndef CAMERA_CAPTURE_RETRY_DELAY_MS
#define CAMERA_CAPTURE_RETRY_DELAY_MS 700UL
#endif

namespace
{
    Preferences prefs;
    CameraScheduleConfig scheduleConfig;
    bool cameraReady = false;
    String lastAutoCaptureKey;

    uint8_t boundedUInt(int value, int fallback, int minValue, int maxValue)
    {
        if (value < minValue || value > maxValue)
            return static_cast<uint8_t>(fallback);
        return static_cast<uint8_t>(value);
    }

    bool jsonBoolOr(JsonObject command, const char *a, const char *b, const char *c, bool fallback)
    {
        if (command.containsKey(a))
            return command[a].as<bool>();
        if (command.containsKey(b))
            return command[b].as<bool>();
        if (command.containsKey(c))
            return command[c].as<bool>();
        return fallback;
    }

    int jsonIntOr(JsonObject command, const char *a, const char *b, const char *c, int fallback)
    {
        if (command.containsKey(a))
            return command[a].as<int>();
        if (command.containsKey(b))
            return command[b].as<int>();
        if (command.containsKey(c))
            return command[c].as<int>();
        return fallback;
    }

    void loadSchedule()
    {
        prefs.begin("camera", true);
        scheduleConfig.enabled = prefs.getBool("enabled", CAMERA_AUTO_CAPTURE_ENABLED);
        scheduleConfig.hour = prefs.getUChar("hour", CAMERA_CAPTURE_HOUR);
        scheduleConfig.minute = prefs.getUChar("minute", CAMERA_CAPTURE_MINUTE);
        scheduleConfig.intervalHours = prefs.getUChar("interval", CAMERA_CAPTURE_INTERVAL_HOURS);
        prefs.end();

        scheduleConfig.hour = boundedUInt(scheduleConfig.hour, CAMERA_CAPTURE_HOUR, 0, 23);
        scheduleConfig.minute = boundedUInt(scheduleConfig.minute, CAMERA_CAPTURE_MINUTE, 0, 59);
        scheduleConfig.intervalHours = boundedUInt(scheduleConfig.intervalHours, CAMERA_CAPTURE_INTERVAL_HOURS, 1, 168);
    }

    void saveSchedule()
    {
        prefs.begin("camera", false);
        prefs.putBool("enabled", scheduleConfig.enabled);
        prefs.putUChar("hour", scheduleConfig.hour);
        prefs.putUChar("minute", scheduleConfig.minute);
        prefs.putUChar("interval", scheduleConfig.intervalHours);
        prefs.end();
    }

    void resetCameraDriver(const char *motivo)
    {
        Serial.printf("[CAMERA][RECOVERY] Reinicializando driver: %s\n", motivo);
        esp_camera_deinit();
        cameraReady = false;
        delay(250);
    }
}

void setupCameraManager()
{
    loadSchedule();
    Serial.printf("[CAMERA][CFG] auto=%s horario=%02u:%02u periodicidade=%uh\n",
                  scheduleConfig.enabled ? "true" : "false",
                  scheduleConfig.hour,
                  scheduleConfig.minute,
                  scheduleConfig.intervalHours);
    initCamera();
}

bool initCamera()
{
    if (cameraReady)
        return true;

    if (CAMERA_PIN_XCLK < 0 || CAMERA_PIN_D0 < 0 || CAMERA_PIN_PCLK < 0)
    {
        Serial.println("[CAMERA][ERRO] CAMERA_PINS nao configurados. Edite firmware_arduino/config.h com o pinout real da placa.");
        return false;
    }

    const bool hasPsram = psramFound();
    Serial.printf("[CAMERA][CFG] psram=%s frame_size=%d quality=%d fb_count=%d xclk=%u retry=%d\n",
                  hasPsram ? "true" : "false",
                  static_cast<int>(CAMERA_FRAME_SIZE),
                  CAMERA_JPEG_QUALITY,
                  CAMERA_FB_COUNT,
                  static_cast<unsigned>(CAMERA_XCLK_FREQ_HZ),
                  CAMERA_CAPTURE_RETRY_COUNT);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = CAMERA_FRAME_SIZE;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count = hasPsram ? CAMERA_FB_COUNT : 1;
    config.fb_location = hasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("[CAMERA][ERRO] esp_camera_init falhou: 0x%x\n", err);
        cameraReady = false;
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr)
    {
        Serial.println("[CAMERA][WARN] esp_camera_sensor_get retornou nulo apos init.");
    }
    else
    {
        Serial.printf("[CAMERA] Sensor detectado PID=0x%04x VER=0x%02x MID=0x%02x%02x\n",
                      sensor->id.PID,
                      sensor->id.VER,
                      sensor->id.MIDH,
                      sensor->id.MIDL);
    }

    cameraReady = true;
    Serial.println("[CAMERA] Inicializada com esp32-camera.");
    delay(300);
    return true;
}

bool captureAndUpload(const char *reason)
{
    if (!initCamera())
        return false;
    if (!isWiFiConnected())
    {
        Serial.println("[CAMERA][UPLOAD][WARN] Wi-Fi indisponivel.");
        return false;
    }

    camera_fb_t *fb = nullptr;
    for (int attempt = 1; attempt <= CAMERA_CAPTURE_RETRY_COUNT; attempt++)
    {
        fb = esp_camera_fb_get();
        if (fb != nullptr && fb->len > 0)
            break;

        if (fb != nullptr)
        {
            Serial.printf("[CAMERA][WARN] frame vazio na tentativa %d/%d.\n", attempt, CAMERA_CAPTURE_RETRY_COUNT);
            esp_camera_fb_return(fb);
            fb = nullptr;
        }
        else
        {
            Serial.printf("[CAMERA][WARN] esp_camera_fb_get nulo na tentativa %d/%d.\n", attempt, CAMERA_CAPTURE_RETRY_COUNT);
        }

        if (attempt == 1 && CAMERA_CAPTURE_RETRY_COUNT > 1)
        {
            resetCameraDriver("fb_get nulo/vazio");
            initCamera();
        }
        delay(CAMERA_CAPTURE_RETRY_DELAY_MS);
    }

    if (fb == nullptr || fb->len == 0)
    {
        Serial.println("[CAMERA][ERRO] Falha ao capturar frame apos retentativas. Verifique pinout, alimentacao, XCLK, PSRAM e resolucao.");
        return false;
    }

    Serial.printf("[CAMERA] Captura OK: %u bytes (%s)\n", static_cast<unsigned>(fb->len), reason);

    const String filename = String("ov5640_") + nowFileTimestamp() + ".jpg";
    const String capturedAt = nowIso8601();

    WiFiClientSecure client;
    client.setInsecure(); // Prototipo: substitua por CA raiz em producao.

    HTTPClient http;
    if (!http.begin(client, CAMERA_UPLOAD_URL))
    {
        Serial.println("[CAMERA][UPLOAD][ERRO] http.begin falhou.");
        esp_camera_fb_return(fb);
        return false;
    }

    http.addHeader("Content-Type", CAMERA_CONTENT_TYPE);
    http.addHeader("x-camera-upload-token", CAMERA_UPLOAD_TOKEN);
    http.addHeader("x-device-id", DEVICE_ID);
    http.addHeader("x-namespace", MQTT_NAMESPACE);
    http.addHeader("x-filename", filename);
    http.addHeader("x-reason", reason);
    http.addHeader("x-captured-at", capturedAt);

    Serial.printf("[CAMERA][UPLOAD] Enviando JPEG binario: arquivo=%s bytes=%u\n",
                  filename.c_str(),
                  static_cast<unsigned>(fb->len));
    const int status = http.POST(fb->buf, fb->len);
    const String response = http.getString();
    http.end();
    esp_camera_fb_return(fb);

    Serial.printf("[CAMERA][UPLOAD] HTTP %d resposta=%s\n", status, response.substring(0, 180).c_str());
    return status >= 200 && status < 300;
}

bool isAutoCaptureDue()
{
    if (!scheduleConfig.enabled || !isTimeValid())
        return false;

    tm timeinfo;
    if (!getLocalTimeSafe(&timeinfo))
        return false;
    if (timeinfo.tm_min != scheduleConfig.minute)
        return false;

    if (scheduleConfig.intervalHours >= 24)
    {
        if (timeinfo.tm_hour != scheduleConfig.hour)
            return false;
    }
    else
    {
        const int diff = (timeinfo.tm_hour - scheduleConfig.hour + 24) % 24;
        if (diff % scheduleConfig.intervalHours != 0)
            return false;
    }

    char key[24];
    strftime(key, sizeof(key), "%Y%m%d%H%M", &timeinfo);
    const String currentKey(key);
    if (currentKey == lastAutoCaptureKey)
        return false;
    lastAutoCaptureKey = currentKey;
    return true;
}

void updateCameraScheduleFromJson(JsonObject command)
{
    scheduleConfig.enabled = jsonBoolOr(command, "habilitado", "enabled", "status", scheduleConfig.enabled);
    scheduleConfig.hour = boundedUInt(jsonIntOr(command, "hora", "hour", "capture_hour", scheduleConfig.hour),
                                      scheduleConfig.hour, 0, 23);
    scheduleConfig.minute = boundedUInt(jsonIntOr(command, "minuto", "minute", "capture_minute", scheduleConfig.minute),
                                        scheduleConfig.minute, 0, 59);
    scheduleConfig.intervalHours = boundedUInt(jsonIntOr(command, "periodicidade_horas", "interval_hours", "capture_interval_hours", scheduleConfig.intervalHours),
                                               scheduleConfig.intervalHours, 1, 168);
    saveSchedule();
    Serial.printf("[CAMERA][CFG] auto=%s horario=%02u:%02u periodicidade=%uh\n",
                  scheduleConfig.enabled ? "true" : "false",
                  scheduleConfig.hour,
                  scheduleConfig.minute,
                  scheduleConfig.intervalHours);
}

CameraScheduleConfig getCameraSchedule()
{
    return scheduleConfig;
}
