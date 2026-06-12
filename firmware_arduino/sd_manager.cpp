#include "sd_manager.h"
#include "config.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <FS.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFiClientSecure.h>
#include <cstring>

#ifndef PIN_SD_CMD
#define PIN_SD_CMD 38
#endif
#ifndef PIN_SD_CLK
#define PIN_SD_CLK 39
#endif
#ifndef PIN_SD_D0
#define PIN_SD_D0 40
#endif
#ifndef SDMMC_FREQUENCY_KHZ
#define SDMMC_FREQUENCY_KHZ 25000
#endif
#ifndef SD_BOOT_SAFE_MODE
#define SD_BOOT_SAFE_MODE 1
#endif
#ifndef SD_CARD_ENABLED
#define SD_CARD_ENABLED 0
#endif
#ifndef SD_MOUNT_ON_BOOT
#define SD_MOUNT_ON_BOOT SD_CARD_ENABLED
#endif
#ifndef SD_PENDING_SYNC_MAX_PER_CYCLE
#define SD_PENDING_SYNC_MAX_PER_CYCLE 1
#endif
#ifndef SD_JSON_HTTP_TIMEOUT_MS
#define SD_JSON_HTTP_TIMEOUT_MS 12000UL
#endif

namespace
{
    const char *IMAGE_DIR = "/imagens";
    const char *LOG_DIR = "/logs";
    const char *QUEUE_DIR = "/fila";
    const char *PENDING_IMAGES_FILE = "/fila/imagens_pendentes.ndjson";
    const char *PENDING_IMAGES_TMP = "/fila/imagens_pendentes.tmp";
    const char *PENDING_JSON_FILE = "/fila/registros_pendentes.ndjson";
    const char *PENDING_JSON_TMP = "/fila/registros_pendentes.tmp";
    bool sdReady = false;

    void ensureDir(const char *path)
    {
        if (!sdReady)
            return;
        if (!SD_MMC.exists(path))
        {
            SD_MMC.mkdir(path);
        }
    }

    bool appendLine(const char *path, const String &line)
    {
        if (!sdReady)
            return false;
        File file = SD_MMC.open(path, FILE_APPEND);
        if (!file)
        {
            Serial.printf("[SD][ERRO] Falha ao abrir %s para append.\n", path);
            return false;
        }
        file.println(line);
        file.flush();
        file.close();
        return true;
    }

    String moduleLogPath(const char *module)
    {
        return String(LOG_DIR) + "/" + module + ".ndjson";
    }

    void writePendingLine(File &file, const String &filename, const String &path, const String &capturedAt, const String &reason, size_t sizeBytes)
    {
        JsonDocument doc;
        doc["filename"] = filename;
        doc["path"] = path;
        doc["captured_at"] = capturedAt;
        doc["reason"] = reason;
        doc["size_bytes"] = static_cast<unsigned long>(sizeBytes);
        doc["queued_at"] = nowIso8601();
        serializeJson(doc, file);
        file.println();
    }

    bool postJsonToFirebase(const char *url, const char *token, const String &payload, String *responsePreview, int *statusCode)
    {
        if (!isWiFiConnected() || url == nullptr || strlen(url) == 0)
            return false;

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(SD_JSON_HTTP_TIMEOUT_MS);
        http.useHTTP10(true);
        if (!http.begin(client, url))
        {
            *statusCode = -1;
            *responsePreview = "http.begin falhou";
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-camera-upload-token", token == nullptr ? "" : token);
        http.addHeader("x-device-id", DEVICE_ID);
        http.addHeader("x-namespace", MQTT_NAMESPACE);
        const int status = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(payload.c_str())), payload.length());
        const String response = http.getString();
        http.end();
        client.stop();
        *statusCode = status;
        *responsePreview = response.substring(0, 120);
        return status >= 200 && status < 300;
    }
}

void setupSdManager()
{
#if SD_BOOT_SAFE_MODE
    sdReady = false;
    Serial.println("[SD][WARN] SD Card em modo seguro por SD_BOOT_SAFE_MODE=1; SD_MMC.begin nao sera chamado.");
    return;
#elif !SD_CARD_ENABLED
    sdReady = false;
    Serial.println("[SD][WARN] SD Card desabilitado por SD_CARD_ENABLED=0; logs locais e fila offline ficam inativos.");
    return;
#elif !SD_MOUNT_ON_BOOT
    sdReady = false;
    Serial.println("[SD][WARN] Montagem SD no boot desabilitada por SD_MOUNT_ON_BOOT=0; evitando SD_MMC.begin no setup.");
    return;
#else
    Serial.printf("[SD] Inicializando SDMMC 1-bit clk=%d cmd=%d d0=%d freq_khz=%d...\n",
                  PIN_SD_CLK,
                  PIN_SD_CMD,
                  PIN_SD_D0,
                  SDMMC_FREQUENCY_KHZ);
    delay(50);
    yield();

    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    yield();
    sdReady = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQUENCY_KHZ);

    if (!sdReady)
    {
        Serial.printf("[SD][WARN] Falha ao montar SDMMC 1-bit clk=%d cmd=%d d0=%d freq_khz=%d. Logs locais desabilitados.\n",
                      PIN_SD_CLK,
                      PIN_SD_CMD,
                      PIN_SD_D0,
                      SDMMC_FREQUENCY_KHZ);
        return;
    }

    ensureDir(IMAGE_DIR);
    ensureDir(LOG_DIR);
    ensureDir(QUEUE_DIR);

    Serial.printf("[SD] SDMMC pronto: clk=%d cmd=%d d0=%d modo=1-bit freq_khz=%d tipo=%u tamanho_mb=%llu\n",
                  PIN_SD_CLK,
                  PIN_SD_CMD,
                  PIN_SD_D0,
                  SDMMC_FREQUENCY_KHZ,
                  static_cast<unsigned>(SD_MMC.cardType()),
                  static_cast<unsigned long long>(SD_MMC.cardSize() / (1024ULL * 1024ULL)));
#endif
}

bool isSdReady()
{
    return sdReady;
}

String sdImagePath(const String &filename)
{
    return String(IMAGE_DIR) + "/" + filename;
}

bool sdSaveCameraImage(const String &filename, const uint8_t *data, size_t length, const String &capturedAt, const char *reason, bool uploaded)
{
    if (!sdReady || data == nullptr || length == 0)
        return false;
    ensureDir(IMAGE_DIR);
    ensureDir(LOG_DIR);

    const String path = sdImagePath(filename);
    if (SD_MMC.exists(path))
        SD_MMC.remove(path);
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.printf("[SD][CAMERA][ERRO] Falha ao abrir %s para escrita.\n", path.c_str());
        return false;
    }

    size_t written = 0;
    while (written < length)
    {
        const size_t chunk = min(static_cast<size_t>(1024), length - written);
        const size_t n = file.write(data + written, chunk);
        if (n == 0)
            break;
        written += n;
        yield();
    }
    file.flush();
    file.close();

    const bool ok = written == length;
    Serial.printf("[SD][CAMERA] imagem=%s bytes=%u gravados=%u ok=%s\n",
                  path.c_str(),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(written),
                  ok ? "true" : "false");

    JsonDocument doc;
    doc["timestamp"] = capturedAt;
    doc["filename"] = filename;
    doc["path"] = path;
    doc["size_bytes"] = static_cast<unsigned long>(length);
    doc["reason"] = reason;
    doc["uploaded"] = uploaded;
    String logLine;
    serializeJson(doc, logLine);
    appendLine("/logs/imagens.ndjson", logLine);
    return ok;
}

bool sdAppendPendingImage(const String &filename, const String &capturedAt, const char *reason, size_t sizeBytes)
{
    if (!sdReady)
        return false;
    ensureDir(QUEUE_DIR);
    const String path = sdImagePath(filename);
    File file = SD_MMC.open(PENDING_IMAGES_FILE, FILE_APPEND);
    if (!file)
    {
        Serial.printf("[SD][PEND][ERRO] Falha ao abrir %s.\n", PENDING_IMAGES_FILE);
        return false;
    }
    writePendingLine(file, filename, path, capturedAt, reason, sizeBytes);
    file.flush();
    file.close();
    Serial.printf("[SD][PEND] imagem pendente enfileirada: %s\n", filename.c_str());
    return true;
}

bool sdAppendLogJson(const char *module, const String &payloadJson)
{
    if (!sdReady)
        return false;
    ensureDir(LOG_DIR);
    return appendLine(moduleLogPath(module).c_str(), payloadJson);
}

bool sdAppendLogDocument(const char *module, JsonDocument &doc)
{
    String line;
    serializeJson(doc, line);
    return sdAppendLogJson(module, line);
}

bool sdQueueFirebaseJson(const char *module, const char *url, const char *token, const String &payloadJson)
{
    if (!sdReady || url == nullptr || strlen(url) == 0)
        return false;
    ensureDir(QUEUE_DIR);
    File file = SD_MMC.open(PENDING_JSON_FILE, FILE_APPEND);
    if (!file)
    {
        Serial.printf("[SD][JSON][ERRO] Falha ao abrir %s.\n", PENDING_JSON_FILE);
        return false;
    }
    JsonDocument doc;
    doc["module"] = module;
    doc["url"] = url;
    doc["token"] = token == nullptr ? "" : token;
    doc["payload"] = payloadJson;
    doc["queued_at"] = nowIso8601();
    serializeJson(doc, file);
    file.println();
    file.flush();
    file.close();
    Serial.printf("[SD][JSON] registro pendente enfileirado: %s\n", module);
    return true;
}

void processPendingSdJsonUploads(uint8_t maxUploads)
{
    if (!sdReady || !isWiFiConnected() || maxUploads == 0)
        return;
    if (!SD_MMC.exists(PENDING_JSON_FILE))
        return;

    File input = SD_MMC.open(PENDING_JSON_FILE, FILE_READ);
    if (!input)
        return;
    SD_MMC.remove(PENDING_JSON_TMP);
    File output = SD_MMC.open(PENDING_JSON_TMP, FILE_WRITE);
    if (!output)
    {
        input.close();
        return;
    }

    uint8_t uploadedCount = 0;
    uint16_t keptCount = 0;
    while (input.available())
    {
        String line = input.readStringUntil('\n');
        line.trim();
        if (line.isEmpty())
            continue;

        bool keep = true;
        if (uploadedCount < maxUploads)
        {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, line);
            if (!error)
            {
                const char *module = doc["module"] | "desconhecido";
                const char *url = doc["url"] | "";
                const char *token = doc["token"] | "";
                const String payload = doc["payload"] | "{}";
                int status = 0;
                String response;
                const bool ok = postJsonToFirebase(url, token, payload, &response, &status);
                Serial.printf("[SD][JSON_SYNC] modulo=%s HTTP %d resposta=%s\n", module, status, response.c_str());
                if (ok)
                {
                    uploadedCount++;
                    keep = false;
                }
            }
        }

        if (keep)
        {
            output.println(line);
            keptCount++;
        }
        yield();
    }

    input.close();
    output.flush();
    output.close();
    SD_MMC.remove(PENDING_JSON_FILE);
    if (keptCount > 0)
    {
        SD_MMC.rename(PENDING_JSON_TMP, PENDING_JSON_FILE);
    }
    else
    {
        SD_MMC.remove(PENDING_JSON_TMP);
    }
}

void processPendingSdImages(SdPendingImageUploader uploader, uint8_t maxUploads)
{
    if (!sdReady || uploader == nullptr || maxUploads == 0)
        return;
    if (!SD_MMC.exists(PENDING_IMAGES_FILE))
        return;

    File input = SD_MMC.open(PENDING_IMAGES_FILE, FILE_READ);
    if (!input)
        return;
    SD_MMC.remove(PENDING_IMAGES_TMP);
    File output = SD_MMC.open(PENDING_IMAGES_TMP, FILE_WRITE);
    if (!output)
    {
        input.close();
        return;
    }

    uint8_t uploadedCount = 0;
    uint16_t keptCount = 0;
    while (input.available())
    {
        String line = input.readStringUntil('\n');
        line.trim();
        if (line.isEmpty())
            continue;

        bool keep = true;
        if (uploadedCount < maxUploads)
        {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, line);
            if (!error)
            {
                const char *path = doc["path"] | "";
                const char *filename = doc["filename"] | "";
                const char *reason = doc["reason"] | "sd_pending";
                const char *capturedAt = doc["captured_at"] | "";
                const size_t sizeBytes = doc["size_bytes"] | 0;
                if (strlen(path) > 0 && strlen(filename) > 0 && SD_MMC.exists(path))
                {
                    const bool ok = uploader(path, filename, reason, capturedAt, sizeBytes);
                    if (ok)
                    {
                        uploadedCount++;
                        keep = false;
                        Serial.printf("[SD][SYNC] imagem sincronizada e removida da fila: %s\n", filename);
                    }
                }
            }
        }

        if (keep)
        {
            output.println(line);
            keptCount++;
        }
        yield();
    }

    input.close();
    output.flush();
    output.close();
    SD_MMC.remove(PENDING_IMAGES_FILE);
    if (keptCount > 0)
    {
        SD_MMC.rename(PENDING_IMAGES_TMP, PENDING_IMAGES_FILE);
    }
    else
    {
        SD_MMC.remove(PENDING_IMAGES_TMP);
    }
}
