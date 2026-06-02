#include "camera_manager.h"
#include "config.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_camera.h>
#include <mbedtls/base64.h>

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

    bool encodeBase64(const uint8_t *data, size_t length, String &out)
    {
        // Algumas versões do mbedTLS no core ESP32 declaram o ponteiro de entrada
        // como uint8_t* mesmo sem modificar o buffer; por isso usamos const_cast aqui.
        uint8_t *input = const_cast<uint8_t *>(data);
        size_t encodedLength = 0;
        int rc = mbedtls_base64_encode(nullptr, 0, &encodedLength, input, length);
        if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        {
            Serial.printf("[CAMERA][B64][ERRO] calculo tamanho rc=%d\n", rc);
            return false;
        }

        uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(encodedLength + 1));
        if (buffer == nullptr)
            buffer = static_cast<uint8_t *>(malloc(encodedLength + 1));
        if (buffer == nullptr)
        {
            Serial.printf("[CAMERA][B64][ERRO] sem memoria para %u bytes\n", static_cast<unsigned>(encodedLength + 1));
            return false;
        }

        rc = mbedtls_base64_encode(buffer, encodedLength + 1, &encodedLength, input, length);
        if (rc != 0)
        {
            Serial.printf("[CAMERA][B64][ERRO] encode rc=%d\n", rc);
            free(buffer);
            return false;
        }

        buffer[encodedLength] = '\0';
        out = reinterpret_cast<char *>(buffer);
        free(buffer);
        return true;
    }

    String jsonEscape(const String &value)
    {
        String escaped;
        escaped.reserve(value.length() + 8);
        for (size_t i = 0; i < value.length(); i++)
        {
            const char c = value.charAt(i);
            if (c == '"' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        return escaped;
    }

    String buildUploadJson(const String &filename, const String &base64Image, const char *reason)
    {
        String payload;
        payload.reserve(base64Image.length() + 512);
        payload += '{';
        payload += "\"namespace\":\"" MQTT_NAMESPACE "\",";
        payload += "\"deviceId\":\"" DEVICE_ID "\",";
        payload += "\"filename\":\"" + jsonEscape(filename) + "\",";
        payload += "\"contentType\":\"" CAMERA_CONTENT_TYPE "\",";
        payload += "\"reason\":\"" + jsonEscape(String(reason)) + "\",";
        payload += "\"capturedAt\":\"" + jsonEscape(nowIso8601()) + "\",";
        payload += "\"metadata\":{\"firmware\":\"arduino\",\"ip\":\"" + jsonEscape(localIpString()) + "\"},";
        payload += "\"imageBase64\":\"" + base64Image + "\"";
        payload += '}';
        return payload;
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
    config.fb_count = CAMERA_FB_COUNT;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("[CAMERA][ERRO] esp_camera_init falhou: 0x%x\n", err);
        cameraReady = false;
        return false;
    }

    cameraReady = true;
    Serial.println("[CAMERA] Inicializada com esp32-camera.");
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

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr)
    {
        Serial.println("[CAMERA][ERRO] esp_camera_fb_get retornou nulo.");
        return false;
    }

    Serial.printf("[CAMERA] Captura OK: %u bytes (%s)\n", static_cast<unsigned>(fb->len), reason);

    String base64Image;
    const bool encoded = encodeBase64(fb->buf, fb->len, base64Image);
    esp_camera_fb_return(fb);
    if (!encoded)
        return false;

    const String filename = String("ov5640_") + nowFileTimestamp() + ".jpg";
    const String body = buildUploadJson(filename, base64Image, reason);

    WiFiClientSecure client;
    client.setInsecure(); // Prototipo: substitua por CA raiz em producao.

    HTTPClient http;
    if (!http.begin(client, CAMERA_UPLOAD_URL))
    {
        Serial.println("[CAMERA][UPLOAD][ERRO] http.begin falhou.");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-camera-upload-token", CAMERA_UPLOAD_TOKEN);
    const int status = http.POST(body);
    const String response = http.getString();
    http.end();

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
