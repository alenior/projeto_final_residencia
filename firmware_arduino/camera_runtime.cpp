#include "camera_manager.h"
#include "config.h"
#include "sd_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <HTTPClient.h>
#include <cstring>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>
#if __has_include(<esp_camera.h>)
#include <esp_camera.h>
#define ESTUFA_HAS_ESP_CAMERA 1
#else
#define ESTUFA_HAS_ESP_CAMERA 0
#define CAMERA_GRAB_WHEN_EMPTY 0
#endif
#include <esp_heap_caps.h>

#ifndef CAMERA_GRAB_MODE
#define CAMERA_GRAB_MODE CAMERA_GRAB_WHEN_EMPTY
#endif
#ifndef CAMERA_USE_PSRAM_FRAMEBUFFER
#define CAMERA_USE_PSRAM_FRAMEBUFFER true
#endif
#ifndef CAMERA_COPY_FRAME_BEFORE_UPLOAD
#define CAMERA_COPY_FRAME_BEFORE_UPLOAD true
#endif
#ifndef CAMERA_DEINIT_BEFORE_UPLOAD
#define CAMERA_DEINIT_BEFORE_UPLOAD true
#endif
#ifndef CAMERA_HTTP_TIMEOUT_MS
#define CAMERA_HTTP_TIMEOUT_MS 20000
#endif
#ifndef CAMERA_CAPTURE_RETRY_COUNT
#define CAMERA_CAPTURE_RETRY_COUNT 3
#endif
#ifndef CAMERA_CAPTURE_RETRY_DELAY_MS
#define CAMERA_CAPTURE_RETRY_DELAY_MS 700UL
#endif

#if ESTUFA_HAS_ESP_CAMERA

#ifndef CAMERA_UPLOAD_BUFFER_INTERNAL_MAX_BYTES
#define CAMERA_UPLOAD_BUFFER_INTERNAL_MAX_BYTES 65536UL
#endif
#ifndef CAMERA_PRE_UPLOAD_SETTLE_MS
#define CAMERA_PRE_UPLOAD_SETTLE_MS 250UL
#endif
#ifndef CAMERA_POST_UPLOAD_SETTLE_MS
#define CAMERA_POST_UPLOAD_SETTLE_MS 500UL
#endif
#ifndef CAMERA_UPLOAD_USE_HTTPCLIENT
#define CAMERA_UPLOAD_USE_HTTPCLIENT false
#endif
#ifndef CAMERA_UPLOAD_CHUNK_BYTES
#define CAMERA_UPLOAD_CHUNK_BYTES 1024UL
#endif
#ifndef SD_PENDING_SYNC_MAX_PER_CYCLE
#define SD_PENDING_SYNC_MAX_PER_CYCLE 1
#endif
#ifndef CAMERA_DIAGNOSTICS_USE_NVS
#define CAMERA_DIAGNOSTICS_USE_NVS false
#endif

#ifndef CAMERA_BRIGHTNESS
#define CAMERA_BRIGHTNESS 0
#endif
#ifndef CAMERA_CONTRAST
#define CAMERA_CONTRAST 1
#endif
#ifndef CAMERA_SATURATION
#define CAMERA_SATURATION 0
#endif
#ifndef CAMERA_AE_LEVEL
#define CAMERA_AE_LEVEL 0
#endif
#ifndef CAMERA_ENABLE_LENC
#define CAMERA_ENABLE_LENC true
#endif
#ifndef CAMERA_ENABLE_RAW_GMA
#define CAMERA_ENABLE_RAW_GMA true
#endif
#ifndef CAMERA_ENABLE_AWB_GAIN
#define CAMERA_ENABLE_AWB_GAIN true
#endif
#ifndef CAMERA_ENABLE_AEC2
#define CAMERA_ENABLE_AEC2 true
#endif

namespace
{
    Preferences prefs;
    Preferences diagPrefs;
    CameraScheduleConfig scheduleConfig;
    bool cameraReady = false;
    String lastAutoCaptureKey;

    void saveCameraDiagnostic(const char *stage, int status = 0)
    {
        if (!CAMERA_DIAGNOSTICS_USE_NVS)
            return;

        if (!diagPrefs.begin("camdiag", false))
        {
            Serial.println("[CAMERA][DIAG][WARN] Nao foi possivel abrir NVS para gravar diagnostico.");
            return;
        }

        diagPrefs.putString("stage", stage);
        diagPrefs.putInt("status", status);
        diagPrefs.putUInt("heap", ESP.getFreeHeap());
        diagPrefs.putUInt("psram", ESP.getFreePsram());
        diagPrefs.end();
    }

    uint8_t boundedUInt(int value, int fallback, int minValue, int maxValue)
    {
        if (value < minValue || value > maxValue)
            return static_cast<uint8_t>(fallback);
        return static_cast<uint8_t>(value);
    }

    void cameraYield()
    {
        // Nao chamar esp_task_wdt_reset() diretamente aqui: no Arduino-ESP32 3.x
        // a tarefa do sketch pode nao estar registrada no TWDT, gerando logs
        // repetitivos "task not found" durante uploads HTTPS longos. yield()+delay(1)
        // mantem a cooperatividade com Wi-Fi/TLS sem acionar esse erro esp-idf.
        yield();
        delay(1);
    }

    uint8_t *allocateUploadBuffer(size_t length, const char **memoryKind)
    {
        if (memoryKind != nullptr)
            *memoryKind = "none";

        uint8_t *buffer = nullptr;
        if (length <= CAMERA_UPLOAD_BUFFER_INTERNAL_MAX_BYTES)
        {
            buffer = static_cast<uint8_t *>(heap_caps_malloc(length, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (buffer != nullptr)
            {
                if (memoryKind != nullptr)
                    *memoryKind = "DRAM_INTERNAL";
                return buffer;
            }

            buffer = static_cast<uint8_t *>(malloc(length));
            if (buffer != nullptr)
            {
                if (memoryKind != nullptr)
                    *memoryKind = "HEAP_DEFAULT";
                return buffer;
            }
        }

        buffer = static_cast<uint8_t *>(ps_malloc(length));
        if (buffer != nullptr && memoryKind != nullptr)
            *memoryKind = "PSRAM";
        return buffer;
    }

    bool parseHttpsUrl(const char *url, String *host, uint16_t *port, String *path)
    {
        const String value(url);
        const String prefix = "https://";
        if (!value.startsWith(prefix))
            return false;

        const int hostStart = prefix.length();
        int pathStart = value.indexOf('/', hostStart);
        if (pathStart < 0)
            pathStart = value.length();

        String hostPort = value.substring(hostStart, pathStart);
        const int colon = hostPort.indexOf(':');
        if (colon >= 0)
        {
            *host = hostPort.substring(0, colon);
            *port = static_cast<uint16_t>(hostPort.substring(colon + 1).toInt());
            if (*port == 0)
                *port = 443;
        }
        else
        {
            *host = hostPort;
            *port = 443;
        }

        *path = pathStart < value.length() ? value.substring(pathStart) : "/";
        return host->length() > 0 && path->length() > 0;
    }

    bool readHttpsResponse(WiFiClientSecure &client, int *statusCode, String *responsePreview)
    {
        const unsigned long deadline = millis() + CAMERA_HTTP_TIMEOUT_MS;
        while (client.connected() && !client.available() && millis() < deadline)
        {
            cameraYield();
            delay(10);
        }

        if (!client.available())
        {
            *statusCode = -1;
            *responsePreview = "sem resposta HTTP";
            return false;
        }

        const String statusLine = client.readStringUntil('\n');
        int parsedStatus = -1;
        if (statusLine.startsWith("HTTP/"))
        {
            const int firstSpace = statusLine.indexOf(' ');
            if (firstSpace > 0)
                parsedStatus = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
        }
        *statusCode = parsedStatus;

        bool inBody = false;
        uint8_t crlfCount = 0;
        responsePreview->reserve(180);
        while ((client.connected() || client.available()) && millis() < deadline && responsePreview->length() < 180)
        {
            while (client.available() && responsePreview->length() < 180)
            {
                const char c = static_cast<char>(client.read());
                if (!inBody)
                {
                    if (c == '\r')
                        continue;
                    if (c == '\n')
                    {
                        crlfCount++;
                        if (crlfCount >= 2)
                            inBody = true;
                    }
                    else
                    {
                        crlfCount = 0;
                    }
                }
                else
                {
                    *responsePreview += c;
                }
            }
            cameraYield();
        }

        if (responsePreview->length() == 0)
            *responsePreview = statusLine;
        return parsedStatus >= 200 && parsedStatus < 300;
    }

    bool uploadJpegManualHttps(
        const uint8_t *imageData,
        size_t imageLength,
        const String &filename,
        const char *reason,
        const String &capturedAt,
        int *statusCode,
        String *responsePreview)
    {
        String host;
        String path;
        uint16_t port = 443;
        if (!parseHttpsUrl(CAMERA_UPLOAD_URL, &host, &port, &path))
        {
            *statusCode = -2;
            *responsePreview = "CAMERA_UPLOAD_URL precisa iniciar com https://";
            return false;
        }

        WiFiClientSecure client;
        client.setInsecure(); // Prototipo: substitua por CA raiz em producao.
        client.setTimeout(CAMERA_HTTP_TIMEOUT_MS / 1000);

        saveCameraDiagnostic("https_connect_start", static_cast<int>(imageLength));
        if (!client.connect(host.c_str(), port))
        {
            *statusCode = -3;
            *responsePreview = "falha ao conectar no host HTTPS";
            client.stop();
            return false;
        }

        saveCameraDiagnostic("https_headers_start", static_cast<int>(imageLength));
        client.printf("POST %s HTTP/1.1\r\n", path.c_str());
        client.printf("Host: %s\r\n", host.c_str());
        client.print("Connection: close\r\n");
        client.printf("Content-Type: %s\r\n", CAMERA_CONTENT_TYPE);
        client.printf("Content-Length: %u\r\n", static_cast<unsigned>(imageLength));
        client.printf("x-camera-upload-token: %s\r\n", CAMERA_UPLOAD_TOKEN);
        client.printf("x-device-id: %s\r\n", DEVICE_ID);
        client.printf("x-namespace: %s\r\n", MQTT_NAMESPACE);
        client.printf("x-filename: %s\r\n", filename.c_str());
        client.printf("x-reason: %s\r\n", reason);
        client.printf("x-captured-at: %s\r\n", capturedAt.c_str());
        client.print("\r\n");

        saveCameraDiagnostic("https_write_start", static_cast<int>(imageLength));
        size_t offset = 0;
        while (offset < imageLength)
        {
            const size_t remaining = imageLength - offset;
            const size_t chunk = remaining < CAMERA_UPLOAD_CHUNK_BYTES ? remaining : CAMERA_UPLOAD_CHUNK_BYTES;
            const size_t written = client.write(imageData + offset, chunk);
            if (written == 0)
            {
                *statusCode = -4;
                *responsePreview = "falha ao escrever chunk HTTPS";
                client.stop();
                return false;
            }
            offset += written;
            cameraYield();
        }
        client.flush();

        saveCameraDiagnostic("https_response_wait", static_cast<int>(imageLength));
        const bool ok = readHttpsResponse(client, statusCode, responsePreview);
        client.stop();
        return ok;
    }

    bool uploadJpegFileManualHttps(
        const char *localPath,
        const String &filename,
        const char *reason,
        const String &capturedAt,
        size_t fallbackLength,
        int *statusCode,
        String *responsePreview)
    {
        String host;
        String path;
        uint16_t port = 443;
        if (!parseHttpsUrl(CAMERA_UPLOAD_URL, &host, &port, &path))
        {
            *statusCode = -2;
            *responsePreview = "CAMERA_UPLOAD_URL precisa iniciar com https://";
            return false;
        }

        File file = SD_MMC.open(localPath, FILE_READ);
        if (!file)
        {
            *statusCode = -6;
            *responsePreview = "falha ao abrir imagem local no SD";
            return false;
        }
        const size_t imageLength = file.size() > 0 ? file.size() : fallbackLength;

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(CAMERA_HTTP_TIMEOUT_MS / 1000);

        if (!client.connect(host.c_str(), port))
        {
            *statusCode = -3;
            *responsePreview = "falha ao conectar no host HTTPS";
            file.close();
            client.stop();
            return false;
        }

        client.printf("POST %s HTTP/1.1\r\n", path.c_str());
        client.printf("Host: %s\r\n", host.c_str());
        client.print("Connection: close\r\n");
        client.printf("Content-Type: %s\r\n", CAMERA_CONTENT_TYPE);
        client.printf("Content-Length: %u\r\n", static_cast<unsigned>(imageLength));
        client.printf("x-camera-upload-token: %s\r\n", CAMERA_UPLOAD_TOKEN);
        client.printf("x-device-id: %s\r\n", DEVICE_ID);
        client.printf("x-namespace: %s\r\n", MQTT_NAMESPACE);
        client.printf("x-filename: %s\r\n", filename.c_str());
        client.printf("x-reason: %s\r\n", reason);
        client.printf("x-captured-at: %s\r\n", capturedAt.c_str());
        client.print("\r\n");

        uint8_t buffer[1024];
        while (file.available())
        {
            const size_t wanted = min(static_cast<size_t>(sizeof(buffer)), static_cast<size_t>(CAMERA_UPLOAD_CHUNK_BYTES));
            const int readBytes = file.read(buffer, wanted);
            if (readBytes <= 0)
                break;
            const size_t written = client.write(buffer, static_cast<size_t>(readBytes));
            if (written != static_cast<size_t>(readBytes))
            {
                *statusCode = -4;
                *responsePreview = "falha ao escrever chunk HTTPS a partir do SD";
                file.close();
                client.stop();
                return false;
            }
            cameraYield();
        }
        file.close();
        client.flush();

        const bool ok = readHttpsResponse(client, statusCode, responsePreview);
        client.stop();
        return ok;
    }

    bool uploadPendingSdImage(const char *path, const char *filename, const char *reason, const char *capturedAt, size_t sizeBytes)
    {
        if (!isWiFiConnected())
            return false;
        int status = 0;
        String response;
        const bool ok = uploadJpegFileManualHttps(path, String(filename), reason, String(capturedAt), sizeBytes, &status, &response);
        Serial.printf("[CAMERA][SD_SYNC] HTTP %d arquivo=%s resposta=%s\n", status, filename, response.substring(0, 120).c_str());
        return ok && status >= 200 && status < 300;
    }

    bool jsonBoolOr(JsonObject command, const char *a, const char *b, const char *c, bool fallback)
    {
        if (command[a].is<bool>())
            return command[a].as<bool>();
        if (command[b].is<bool>())
            return command[b].as<bool>();
        if (command[c].is<bool>())
            return command[c].as<bool>();
        return fallback;
    }

    int jsonIntOr(JsonObject command, const char *a, const char *b, const char *c, int fallback)
    {
        if (command[a].is<int>())
            return command[a].as<int>();
        if (command[b].is<int>())
            return command[b].as<int>();
        if (command[c].is<int>())
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

    void prepareCameraControlPins()
    {
        if (CAMERA_PIN_PWDN >= 0)
        {
            pinMode(CAMERA_PIN_PWDN, OUTPUT);
            digitalWrite(CAMERA_PIN_PWDN, LOW);
        }
        if (CAMERA_PIN_RESET >= 0)
        {
            pinMode(CAMERA_PIN_RESET, OUTPUT);
            digitalWrite(CAMERA_PIN_RESET, LOW);
            delay(20);
            digitalWrite(CAMERA_PIN_RESET, HIGH);
        }
        delay(30);
    }

    void deinitCameraDriver(const char *motivo)
    {
        if (!cameraReady)
            return;
        Serial.printf("[CAMERA] Desinicializando driver: %s\n", motivo);
        esp_camera_deinit();
        cameraReady = false;
    }

    void resetCameraDriver(const char *motivo)
    {
        Serial.printf("[CAMERA][RECOVERY] Reinicializando driver: %s\n", motivo);
        esp_camera_deinit();
        cameraReady = false;
        delay(250);
        prepareCameraControlPins();
    }
}

void printCameraUploadDiagnostic()
{
    if (!CAMERA_DIAGNOSTICS_USE_NVS)
    {
        Serial.println("[CAMERA][LAST] diagnostico NVS desabilitado (CAMERA_DIAGNOSTICS_USE_NVS=false).");
        return;
    }

    if (!diagPrefs.begin("camdiag", true))
    {
        Serial.println("[CAMERA][LAST][WARN] Nao foi possivel abrir NVS camdiag.");
        return;
    }

    const String stage = diagPrefs.getString("stage", "nenhum");
    const int status = diagPrefs.getInt("status", 0);
    const uint32_t heap = diagPrefs.getUInt("heap", 0);
    const uint32_t psram = diagPrefs.getUInt("psram", 0);
    diagPrefs.end();

    Serial.printf("[CAMERA][LAST] stage=%s status=%d heap=%lu psram=%lu\n",
                  stage.c_str(),
                  status,
                  static_cast<unsigned long>(heap),
                  static_cast<unsigned long>(psram));
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

    prepareCameraControlPins();

    const bool hasPsram = psramFound();
    const bool usePsramFramebuffer = hasPsram && CAMERA_USE_PSRAM_FRAMEBUFFER;
    Serial.printf("[CAMERA][CFG] psram=%s fb_location=%s frame_size=%d quality=%d fb_count=%d xclk=%u grab_mode=%d retry=%d copy_before_upload=%s deinit_before_upload=%s\n",
                  hasPsram ? "true" : "false",
                  usePsramFramebuffer ? "PSRAM" : "DRAM",
                  static_cast<int>(CAMERA_FRAME_SIZE),
                  CAMERA_JPEG_QUALITY,
                  CAMERA_FB_COUNT,
                  static_cast<unsigned>(CAMERA_XCLK_FREQ_HZ),
                  static_cast<int>(CAMERA_GRAB_MODE),
                  CAMERA_CAPTURE_RETRY_COUNT,
                  CAMERA_COPY_FRAME_BEFORE_UPLOAD ? "true" : "false",
                  CAMERA_DEINIT_BEFORE_UPLOAD ? "true" : "false");
    Serial.printf("[CAMERA][UPLOAD_CFG] internal_buffer_max=%lu pre_settle=%lu post_settle=%lu http_timeout=%lu\n",
                  static_cast<unsigned long>(CAMERA_UPLOAD_BUFFER_INTERNAL_MAX_BYTES),
                  static_cast<unsigned long>(CAMERA_PRE_UPLOAD_SETTLE_MS),
                  static_cast<unsigned long>(CAMERA_POST_UPLOAD_SETTLE_MS),
                  static_cast<unsigned long>(CAMERA_HTTP_TIMEOUT_MS));
    Serial.printf("[CAMERA][PINS] pwdn=%d reset=%d xclk=%d siod=%d sioc=%d d0=%d d1=%d d2=%d d3=%d d4=%d d5=%d d6=%d d7=%d vsync=%d href=%d pclk=%d\n",
                  CAMERA_PIN_PWDN, CAMERA_PIN_RESET, CAMERA_PIN_XCLK, CAMERA_PIN_SIOD, CAMERA_PIN_SIOC,
                  CAMERA_PIN_D0, CAMERA_PIN_D1, CAMERA_PIN_D2, CAMERA_PIN_D3, CAMERA_PIN_D4, CAMERA_PIN_D5,
                  CAMERA_PIN_D6, CAMERA_PIN_D7, CAMERA_PIN_VSYNC, CAMERA_PIN_HREF, CAMERA_PIN_PCLK);

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
    config.fb_location = usePsramFramebuffer ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_MODE;

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
        sensor->set_framesize(sensor, CAMERA_FRAME_SIZE);
        sensor->set_quality(sensor, CAMERA_JPEG_QUALITY);
        if (sensor->set_brightness)
            sensor->set_brightness(sensor, CAMERA_BRIGHTNESS);
        if (sensor->set_contrast)
            sensor->set_contrast(sensor, CAMERA_CONTRAST);
        if (sensor->set_saturation)
            sensor->set_saturation(sensor, CAMERA_SATURATION);
        if (sensor->set_ae_level)
            sensor->set_ae_level(sensor, CAMERA_AE_LEVEL);
        if (sensor->set_lenc)
            sensor->set_lenc(sensor, CAMERA_ENABLE_LENC ? 1 : 0);
        if (sensor->set_raw_gma)
            sensor->set_raw_gma(sensor, CAMERA_ENABLE_RAW_GMA ? 1 : 0);
        if (sensor->set_awb_gain)
            sensor->set_awb_gain(sensor, CAMERA_ENABLE_AWB_GAIN ? 1 : 0);
        if (sensor->set_aec2)
            sensor->set_aec2(sensor, CAMERA_ENABLE_AEC2 ? 1 : 0);
        Serial.printf("[CAMERA][QUALIDADE] brightness=%d contrast=%d saturation=%d ae_level=%d lenc=%s raw_gma=%s awb_gain=%s aec2=%s\n",
                      CAMERA_BRIGHTNESS,
                      CAMERA_CONTRAST,
                      CAMERA_SATURATION,
                      CAMERA_AE_LEVEL,
                      CAMERA_ENABLE_LENC ? "on" : "off",
                      CAMERA_ENABLE_RAW_GMA ? "on" : "off",
                      CAMERA_ENABLE_AWB_GAIN ? "on" : "off",
                      CAMERA_ENABLE_AEC2 ? "on" : "off");
    }

    cameraReady = true;
    Serial.println("[CAMERA] Inicializada com esp32-camera.");
    delay(300);
    return true;
}

bool captureAndUpload(const char *reason)
{
    saveCameraDiagnostic("capture_start");
    if (!initCamera())
    {
        saveCameraDiagnostic("init_failed");
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
        cameraYield();
        delay(CAMERA_CAPTURE_RETRY_DELAY_MS);
    }

    if (fb == nullptr || fb->len == 0)
    {
        Serial.println("[CAMERA][ERRO] Falha ao capturar frame apos retentativas. Verifique pinout, alimentacao, XCLK, PSRAM e resolucao.");
        saveCameraDiagnostic("frame_failed");
        return false;
    }

    saveCameraDiagnostic("frame_ok", static_cast<int>(fb->len));
    Serial.printf("[CAMERA] Captura OK: %u bytes (%s) heap=%lu psram=%lu\n",
                  static_cast<unsigned>(fb->len),
                  reason,
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getFreePsram()));

    const String filename = String("ov5640_") + nowFileTimestamp() + ".jpg";
    const String capturedAt = nowIso8601();
    const size_t imageLength = fb->len;
    uint8_t *copiedImage = nullptr;
    const uint8_t *imageData = fb->buf;

    if (CAMERA_COPY_FRAME_BEFORE_UPLOAD)
    {
        const char *memoryKind = "none";
        copiedImage = allocateUploadBuffer(imageLength, &memoryKind);

        if (copiedImage != nullptr)
        {
            memcpy(copiedImage, fb->buf, imageLength);
            imageData = copiedImage;
            esp_camera_fb_return(fb);
            fb = nullptr;
            Serial.printf("[CAMERA] Frame copiado para buffer de upload: bytes=%u memoria=%s heap=%lu psram=%lu\n",
                          static_cast<unsigned>(imageLength),
                          memoryKind,
                          static_cast<unsigned long>(ESP.getFreeHeap()),
                          static_cast<unsigned long>(ESP.getFreePsram()));
            saveCameraDiagnostic("frame_copied", static_cast<int>(imageLength));
            cameraYield();
            if (CAMERA_DEINIT_BEFORE_UPLOAD)
            {
                deinitCameraDriver("frame copiado antes do upload");
                saveCameraDiagnostic("camera_deinit_before_upload", static_cast<int>(imageLength));
                delay(CAMERA_PRE_UPLOAD_SETTLE_MS);
                cameraYield();
            }
        }
        else
        {
            Serial.println("[CAMERA][WARN] Sem memoria para copiar frame; upload usara framebuffer ativo.");
            saveCameraDiagnostic("frame_copy_failed", static_cast<int>(imageLength));
        }
    }

    const bool savedLocal = sdSaveCameraImage(filename, imageData, imageLength, capturedAt, reason, false);
    bool queuedPending = false;
    if (!savedLocal)
    {
        Serial.println("[CAMERA][SD][WARN] Imagem nao foi salva localmente no SD.");
    }

    int status = 0;
    String response;
    bool ok = false;

    if (!isWiFiConnected())
    {
        Serial.println("[CAMERA][UPLOAD][WARN] Wi-Fi indisponivel; imagem ficara pendente no SD.");
        saveCameraDiagnostic("wifi_unavailable");
        if (savedLocal)
            queuedPending = sdAppendPendingImage(filename, capturedAt, reason, imageLength);
    }
    else
    {
        cameraYield();
        Serial.printf("[CAMERA][UPLOAD] Enviando JPEG binario: arquivo=%s bytes=%u modo=%s heap=%lu psram=%lu\n",
                      filename.c_str(),
                      static_cast<unsigned>(imageLength),
                      CAMERA_UPLOAD_USE_HTTPCLIENT ? "HTTPClient" : "HTTPS_CHUNKED",
                      static_cast<unsigned long>(ESP.getFreeHeap()),
                      static_cast<unsigned long>(ESP.getFreePsram()));

        if (CAMERA_UPLOAD_USE_HTTPCLIENT)
        {
            WiFiClientSecure client;
            client.setInsecure(); // Prototipo: substitua por CA raiz em producao.

            HTTPClient http;
            http.setReuse(false);
            http.setTimeout(CAMERA_HTTP_TIMEOUT_MS);
            http.useHTTP10(true);
            if (!http.begin(client, CAMERA_UPLOAD_URL))
            {
                Serial.println("[CAMERA][UPLOAD][ERRO] http.begin falhou.");
                saveCameraDiagnostic("http_begin_failed");
                status = -5;
                response = "http.begin falhou";
            }
            else
            {
                http.addHeader("Content-Type", CAMERA_CONTENT_TYPE);
                http.addHeader("x-camera-upload-token", CAMERA_UPLOAD_TOKEN);
                http.addHeader("x-device-id", DEVICE_ID);
                http.addHeader("x-namespace", MQTT_NAMESPACE);
                http.addHeader("x-filename", filename);
                http.addHeader("x-reason", reason);
                http.addHeader("x-captured-at", capturedAt);

                saveCameraDiagnostic("http_post_start", static_cast<int>(imageLength));
                status = http.POST(const_cast<uint8_t *>(imageData), imageLength);
                saveCameraDiagnostic("http_post_done", status);
                cameraYield();
                response = http.getString();
                http.end();
                client.stop();
                ok = status >= 200 && status < 300;
            }
        }
        else
        {
            ok = uploadJpegManualHttps(imageData, imageLength, filename, reason, capturedAt, &status, &response);
            ok = ok && status >= 200 && status < 300;
        }
    }

    if (savedLocal && !ok && !queuedPending)
    {
        sdAppendPendingImage(filename, capturedAt, reason, imageLength);
    }

    if (fb != nullptr)
        esp_camera_fb_return(fb);
    if (copiedImage != nullptr)
        free(copiedImage);
    delay(CAMERA_POST_UPLOAD_SETTLE_MS);
    cameraYield();

    Serial.printf("[CAMERA][UPLOAD] HTTP %d resposta=%s heap=%lu psram=%lu\n",
                  status,
                  response.substring(0, 180).c_str(),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getFreePsram()));
    saveCameraDiagnostic(ok ? "upload_success" : "upload_http_error", status);
    return ok;
}

void processPendingCameraUploads()
{
    if (!isWiFiConnected() || !isSdReady())
        return;
    processPendingSdImages(uploadPendingSdImage, SD_PENDING_SYNC_MAX_PER_CYCLE);
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

#else

void printCameraUploadDiagnostic()
{
    Serial.println("[CAMERA][WARN] esp_camera.h nao encontrado; suporte OV5640 desabilitado nesta compilacao.");
}

void setupCameraManager()
{
    Serial.println("[CAMERA][WARN] esp_camera.h nao encontrado. Instale/seleciona um core/placa ESP32 com esp32-camera para habilitar OV5640.");
}

bool initCamera()
{
    Serial.println("[CAMERA][ERRO] initCamera ignorado: esp_camera.h ausente.");
    return false;
}

bool captureAndUpload(const char *reason)
{
    Serial.printf("[CAMERA][ERRO] Captura '%s' indisponivel: esp_camera.h ausente.\n", reason);
    return false;
}

void processPendingCameraUploads()
{
}

bool isAutoCaptureDue()
{
    return false;
}

void updateCameraScheduleFromJson(JsonObject command)
{
    (void)command;
    Serial.println("[CAMERA][WARN] Configuracao ignorada: esp_camera.h ausente.");
}

CameraScheduleConfig getCameraSchedule()
{
    return CameraScheduleConfig{false, CAMERA_CAPTURE_HOUR, CAMERA_CAPTURE_MINUTE, CAMERA_CAPTURE_INTERVAL_HOURS};
}

#endif
