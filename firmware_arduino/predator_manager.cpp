#include "predator_manager.h"
#include "actuators.h"
#include "config.h"
#include "sd_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <cstring>

#ifndef PREDATOR_INGEST_URL
#define PREDATOR_INGEST_URL ""
#endif
#ifndef PREDATOR_UPLOAD_TOKEN
#define PREDATOR_UPLOAD_TOKEN CAMERA_UPLOAD_TOKEN
#endif
#ifndef PIN_BUZZER
#define PIN_BUZZER 3
#endif
#ifndef BUZZER_PWM_FREQ_HZ
#define BUZZER_PWM_FREQ_HZ 5000
#endif
#ifndef BUZZER_PWM_RESOLUTION_BITS
#define BUZZER_PWM_RESOLUTION_BITS 10
#endif
#ifndef BUZZER_PWM_DUTY
#define BUZZER_PWM_DUTY 512
#endif
#ifndef BUZZER_PWM_CHANNEL
#define BUZZER_PWM_CHANNEL 7
#endif
#ifndef PREDATOR_CHECK_INTERVAL_MS
#define PREDATOR_CHECK_INTERVAL_MS 500UL
#endif
#ifndef PREDATOR_ALERT_COOLDOWN_MS
#define PREDATOR_ALERT_COOLDOWN_MS 30000UL
#endif
#ifndef PREDATOR_BUZZER_DURATION_MS
#define PREDATOR_BUZZER_DURATION_MS 5000UL
#endif
#ifndef PREDATOR_HTTP_TIMEOUT_MS
#define PREDATOR_HTTP_TIMEOUT_MS 12000UL
#endif
#ifndef PREDATOR_POST_UPLOAD_SETTLE_MS
#define PREDATOR_POST_UPLOAD_SETTLE_MS 100UL
#endif
#ifndef PREDATOR_UPLOAD_CHUNK_BYTES
#define PREDATOR_UPLOAD_CHUNK_BYTES 256UL
#endif
#ifndef TLS_UPLOAD_MIN_SPACING_MS
#define TLS_UPLOAD_MIN_SPACING_MS 3000UL
#endif
#ifndef NETWORK_UPLOADS_ENABLED
#define NETWORK_UPLOADS_ENABLED 0
#endif
#ifndef PREDATOR_UPLOAD_ENABLED
#define PREDATOR_UPLOAD_ENABLED 0
#endif

namespace
{
    constexpr unsigned long MIN_PREDATOR_CHECK_INTERVAL_MS = 100UL;
    constexpr unsigned long MAX_PREDATOR_CHECK_INTERVAL_MS = 60000UL;
    constexpr unsigned long MIN_PREDATOR_ALERT_COOLDOWN_MS = 5000UL;
    constexpr unsigned long MAX_PREDATOR_ALERT_COOLDOWN_MS = 3600000UL;
    constexpr unsigned long MIN_PREDATOR_BUZZER_DURATION_MS = 500UL;
    constexpr unsigned long MAX_PREDATOR_BUZZER_DURATION_MS = 60000UL;
    constexpr int MIN_BUZZER_DUTY = 0;
    constexpr int MAX_BUZZER_DUTY = (1 << BUZZER_PWM_RESOLUTION_BITS) - 1;

    struct PredatorReading
    {
        bool valid;
        bool motionDetected;
        bool monitoringEnabled;
        bool buzzerEnabled;
        bool alarmActive;
        bool alertEvent;
        const char *reason;
        unsigned long checkIntervalMs;
        unsigned long alertCooldownMs;
        unsigned long buzzerDurationMs;
        int buzzerDuty;
    };

    bool monitoringEnabled = true;
    bool buzzerEnabled = true;
    bool lastMotionState = false;
    bool buzzerAttached = false;
    unsigned long checkIntervalMs = PREDATOR_CHECK_INTERVAL_MS;
    unsigned long alertCooldownMs = PREDATOR_ALERT_COOLDOWN_MS;
    unsigned long buzzerDurationMs = PREDATOR_BUZZER_DURATION_MS;
    unsigned long lastCheckMs = 0;
    unsigned long lastAlertMs = 0;
    unsigned long buzzerOffAtMs = 0;
    int buzzerDuty = BUZZER_PWM_DUTY;

    PredatorReading lastReading = {
        false,
        false,
        true,
        true,
        false,
        false,
        "boot",
        PREDATOR_CHECK_INTERVAL_MS,
        PREDATOR_ALERT_COOLDOWN_MS,
        PREDATOR_BUZZER_DURATION_MS,
        BUZZER_PWM_DUTY,
    };

    void predatorYield()
    {
        yield();
        delay(1);
    }

    unsigned long clampULong(unsigned long value, unsigned long minValue, unsigned long maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    int clampInt(int value, int minValue, int maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    bool elapsedSince(unsigned long previousMs, unsigned long intervalMs)
    {
        return static_cast<long>(millis() - previousMs) >= static_cast<long>(intervalMs);
    }

    void fillPredatorMetadata(PredatorReading *reading)
    {
        reading->monitoringEnabled = monitoringEnabled;
        reading->buzzerEnabled = buzzerEnabled;
        reading->alarmActive = buzzerOffAtMs != 0;
        reading->checkIntervalMs = checkIntervalMs;
        reading->alertCooldownMs = alertCooldownMs;
        reading->buzzerDurationMs = buzzerDurationMs;
        reading->buzzerDuty = buzzerDuty;
    }

    void stopBuzzer(const char *reason)
    {
        if (PIN_BUZZER >= 0 && buzzerAttached)
        {
            ledcWrite(PIN_BUZZER, 0);
        }
        buzzerOffAtMs = 0;
        Serial.printf("[PREDADORES][BUZZER] OFF motivo=%s\n", reason);
    }

    void startBuzzer(const char *reason)
    {
        if (!buzzerEnabled || PIN_BUZZER < 0 || !buzzerAttached || buzzerDuty <= 0)
        {
            Serial.printf("[PREDADORES][BUZZER][WARN] Buzzer nao acionado motivo=%s enabled=%s pin=%d attached=%s duty=%d\n",
                          reason,
                          buzzerEnabled ? "true" : "false",
                          PIN_BUZZER,
                          buzzerAttached ? "true" : "false",
                          buzzerDuty);
            return;
        }

        ledcWrite(PIN_BUZZER, buzzerDuty);
        buzzerOffAtMs = millis() + buzzerDurationMs;
        Serial.printf("[PREDADORES][BUZZER] ON motivo=%s freq=%d duty=%d duracao_ms=%lu upload_global=%d upload_predadores=%d\n",
                      reason,
                      BUZZER_PWM_FREQ_HZ,
                      buzzerDuty,
                      buzzerDurationMs,
                      NETWORK_UPLOADS_ENABLED,
                      PREDATOR_UPLOAD_ENABLED);
    }

    void stopBuzzerIfDue()
    {
        if (buzzerOffAtMs == 0)
            return;
        if (static_cast<long>(millis() - buzzerOffAtMs) < 0)
            return;
        stopBuzzer("timeout");

        lastReading.alarmActive = false;
        lastReading.reason = "buzzer_timeout";
        fillPredatorMetadata(&lastReading);
    }

    PredatorReading readPredatorSensor(const char *reason, bool alertEvent = false)
    {
        PredatorReading reading = lastReading;
        reading.valid = true;
        reading.motionDetected = readMotion();
        reading.alertEvent = alertEvent;
        reading.reason = reason;
        fillPredatorMetadata(&reading);
        return reading;
    }

    String buildPredatorPayload(const PredatorReading &reading)
    {
        JsonDocument doc;
        doc["deviceId"] = DEVICE_ID;
        doc["namespace"] = MQTT_NAMESPACE;
        doc["timestamp"] = nowIso8601();
        doc["uptime_ms"] = millis();
        doc["event_id"] = String("predadores_") + String(millis());
        doc["motion_detected"] = reading.motionDetected;
        doc["monitoring_enabled"] = reading.monitoringEnabled;
        doc["buzzer_enabled"] = reading.buzzerEnabled;
        doc["alarm_active"] = reading.alarmActive;
        doc["alert_event"] = reading.alertEvent;
        doc["reason"] = reading.reason;
        doc["pir_pin"] = PIN_PIR;
        doc["buzzer_pin"] = PIN_BUZZER;
        doc["buzzer_pwm_freq_hz"] = BUZZER_PWM_FREQ_HZ;
        doc["buzzer_pwm_resolution_bits"] = BUZZER_PWM_RESOLUTION_BITS;
        doc["buzzer_pwm_duty"] = reading.buzzerDuty;
        doc["check_interval_ms"] = reading.checkIntervalMs;
        doc["alert_cooldown_ms"] = reading.alertCooldownMs;
        doc["buzzer_duration_ms"] = reading.buzzerDurationMs;
        doc["source"] = "esp32_s3_predator_monitor";

        String output;
        serializeJson(doc, output);
        return output;
    }

    bool parsePredatorHttpsUrl(const char *url, String *host, uint16_t *port, String *path)
    {
        const String rawUrl(url);
        const String scheme = "https://";
        if (!rawUrl.startsWith(scheme))
        {
            Serial.printf("[PREDADORES][UPLOAD][ERRO] URL deve iniciar com https://: %s\n", url);
            return false;
        }

        String remainder = rawUrl.substring(scheme.length());
        const int slashIndex = remainder.indexOf('/');
        String authority = remainder;
        *path = "/";
        if (slashIndex >= 0)
        {
            authority = remainder.substring(0, slashIndex);
            *path = remainder.substring(slashIndex);
        }

        const int colonIndex = authority.lastIndexOf(':');
        *port = 443;
        if (colonIndex > 0)
        {
            *host = authority.substring(0, colonIndex);
            *port = static_cast<uint16_t>(authority.substring(colonIndex + 1).toInt());
            if (*port == 0)
                *port = 443;
        }
        else
        {
            *host = authority;
        }

        if (host->isEmpty())
        {
            Serial.println("[PREDADORES][UPLOAD][ERRO] Host HTTPS vazio.");
            return false;
        }
        return true;
    }

    bool readPredatorHttpsResponse(WiFiClientSecure *client, int *statusCode, String *responsePreview)
    {
        const unsigned long deadlineMs = millis() + PREDATOR_HTTP_TIMEOUT_MS;
        *statusCode = -1;
        responsePreview->remove(0);

        while (client->connected() && !client->available() && static_cast<long>(deadlineMs - millis()) > 0)
        {
            predatorYield();
        }

        if (!client->available())
        {
            Serial.println("[PREDADORES][UPLOAD][ERRO] Timeout aguardando resposta HTTPS.");
            return false;
        }

        String statusLine = client->readStringUntil('\n');
        statusLine.trim();
        if (statusLine.startsWith("HTTP/"))
        {
            const int firstSpace = statusLine.indexOf(' ');
            if (firstSpace > 0 && statusLine.length() >= firstSpace + 4)
            {
                *statusCode = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
            }
        }

        while (static_cast<long>(deadlineMs - millis()) > 0)
        {
            if (!client->available())
            {
                if (!client->connected())
                    break;
                predatorYield();
                continue;
            }
            String header = client->readStringUntil('\n');
            header.trim();
            if (header.length() == 0)
                break;
        }

        while (static_cast<long>(deadlineMs - millis()) > 0 && (client->connected() || client->available()))
        {
            while (client->available())
            {
                const char c = static_cast<char>(client->read());
                if (responsePreview->length() < 160)
                    *responsePreview += c;
            }
            if (!client->connected())
                break;
            predatorYield();
        }
        responsePreview->trim();
        return *statusCode >= 0;
    }

    bool postPredatorReading(const PredatorReading &reading)
    {
        const String payload = buildPredatorPayload(reading);
        sdAppendLogJson("predadores", payload);

#if !NETWORK_UPLOADS_ENABLED || !PREDATOR_UPLOAD_ENABLED
        Serial.println("[PREDADORES][UPLOAD][WARN] Upload HTTPS desabilitado por NETWORK_UPLOADS_ENABLED=0 ou PREDATOR_UPLOAD_ENABLED=0; alerta local mantido sem POST.");
        return false;
#endif

        if (strlen(PREDATOR_INGEST_URL) == 0)
        {
            Serial.println("[PREDADORES][UPLOAD][WARN] PREDATOR_INGEST_URL vazio; alerta mantido apenas no SD.");
            return false;
        }
        if (!isWiFiConnected())
        {
            Serial.println("[PREDADORES][UPLOAD][WARN] Wi-Fi indisponivel; alerta mantido no SD.");
            sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
            return false;
        }
        if (!tlsUploadSpacingElapsed(TLS_UPLOAD_MIN_SPACING_MS))
        {
            Serial.println("[PREDADORES][UPLOAD][SKIP] Outra sessao TLS recente; adiando para evitar corrupcao de heap.");
            sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
            return false;
        }

        String host;
        String path;
        uint16_t port;
        if (!parsePredatorHttpsUrl(PREDATOR_INGEST_URL, &host, &port, &path))
        {
            sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
            return false;
        }

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(1);

        Serial.printf("[PREDADORES][UPLOAD] TLS raw host=%s porta=%u path=%s bytes=%u chunk=%lu heap=%lu movimento=%s alarme=%s motivo=%s\n",
                      host.c_str(),
                      port,
                      path.c_str(),
                      static_cast<unsigned>(payload.length()),
                      static_cast<unsigned long>(PREDATOR_UPLOAD_CHUNK_BYTES),
                      static_cast<unsigned long>(ESP.getFreeHeap()),
                      reading.motionDetected ? "true" : "false",
                      reading.alarmActive ? "true" : "false",
                      reading.reason);

        if (!client.connect(host.c_str(), port))
        {
            Serial.println("[PREDADORES][UPLOAD][ERRO] Falha ao conectar TLS raw.");
            client.stop();
            sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
            return false;
        }

        client.print(String("POST ") + path + " HTTP/1.1\r\n");
        client.print(String("Host: ") + host + "\r\n");
        client.print("User-Agent: EstufaIoT-ESP32S3/1.0\r\n");
        client.print("Connection: close\r\n");
        client.print("Content-Type: application/json\r\n");
        client.print(String("x-camera-upload-token: ") + PREDATOR_UPLOAD_TOKEN + "\r\n");
        client.print(String("x-device-id: ") + DEVICE_ID + "\r\n");
        client.print(String("x-namespace: ") + MQTT_NAMESPACE + "\r\n");
        client.print(String("Content-Length: ") + String(payload.length()) + "\r\n\r\n");

        size_t offset = 0;
        const size_t configuredChunkBytes = static_cast<size_t>(PREDATOR_UPLOAD_CHUNK_BYTES);
        const size_t chunkBytes = configuredChunkBytes == 0 ? 1 : configuredChunkBytes;
        while (offset < payload.length())
        {
            const size_t remaining = payload.length() - offset;
            const size_t toWrite = remaining < chunkBytes ? remaining : chunkBytes;
            const size_t written = client.write(reinterpret_cast<const uint8_t *>(payload.c_str() + offset), toWrite);
            if (written == 0)
            {
                Serial.println("[PREDADORES][UPLOAD][ERRO] Escrita TLS raw retornou 0 byte.");
                client.stop();
                sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
                return false;
            }
            offset += written;
            predatorYield();
        }
        client.flush();

        int status = -1;
        String response;
        const bool responseRead = readPredatorHttpsResponse(&client, &status, &response);
        client.stop();
        noteTlsUploadFinished();
        delay(PREDATOR_POST_UPLOAD_SETTLE_MS);
        predatorYield();

        const bool ok = responseRead && status >= 200 && status < 300;
        Serial.printf("[PREDADORES][UPLOAD] HTTP %d resposta=%s\n", status, response.c_str());
        if (!ok)
            sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
        return ok;
    }

    unsigned long jsonULongOr(JsonObject command, const char *a, const char *b, unsigned long fallback)
    {
        if (command[a].is<unsigned long>() || command[a].is<int>())
            return command[a].as<unsigned long>();
        if (command[b].is<unsigned long>() || command[b].is<int>())
            return command[b].as<unsigned long>();
        return fallback;
    }

    int jsonIntOr(JsonObject command, const char *a, const char *b, int fallback)
    {
        if (command[a].is<int>())
            return command[a].as<int>();
        if (command[b].is<int>())
            return command[b].as<int>();
        return fallback;
    }

    bool jsonBoolOr(JsonObject command, const char *a, const char *b, bool fallback)
    {
        if (!command[a].isNull())
            return command[a].as<bool>();
        if (!command[b].isNull())
            return command[b].as<bool>();
        return fallback;
    }

    void updatePredatorConfigFromJson(JsonObject command)
    {
        monitoringEnabled = jsonBoolOr(command, "monitoring_enabled", "monitoramento_habilitado", monitoringEnabled);
        buzzerEnabled = jsonBoolOr(command, "buzzer_enabled", "buzzer_habilitado", buzzerEnabled);
        checkIntervalMs = clampULong(
            jsonULongOr(command, "predator_check_interval_ms", "intervalo_verificacao_predadores_ms", checkIntervalMs),
            MIN_PREDATOR_CHECK_INTERVAL_MS,
            MAX_PREDATOR_CHECK_INTERVAL_MS);
        alertCooldownMs = clampULong(
            jsonULongOr(command, "predator_alert_cooldown_ms", "cooldown_alerta_predadores_ms", alertCooldownMs),
            MIN_PREDATOR_ALERT_COOLDOWN_MS,
            MAX_PREDATOR_ALERT_COOLDOWN_MS);
        buzzerDurationMs = clampULong(
            jsonULongOr(command, "buzzer_duration_ms", "duracao_buzzer_ms", buzzerDurationMs),
            MIN_PREDATOR_BUZZER_DURATION_MS,
            MAX_PREDATOR_BUZZER_DURATION_MS);
        buzzerDuty = clampInt(jsonIntOr(command, "buzzer_pwm_duty", "duty_buzzer", buzzerDuty), MIN_BUZZER_DUTY, MAX_BUZZER_DUTY);

        if (!buzzerEnabled)
            stopBuzzer("config_disabled");

        Serial.printf("[PREDADORES][CFG] monitor=%s buzzer=%s check_ms=%lu cooldown_ms=%lu duracao_ms=%lu duty=%d\n",
                      monitoringEnabled ? "true" : "false",
                      buzzerEnabled ? "true" : "false",
                      checkIntervalMs,
                      alertCooldownMs,
                      buzzerDurationMs,
                      buzzerDuty);

        lastReading.reason = "config_updated";
        fillPredatorMetadata(&lastReading);
    }
}

void setupPredatorManager()
{
    checkIntervalMs = clampULong(checkIntervalMs, MIN_PREDATOR_CHECK_INTERVAL_MS, MAX_PREDATOR_CHECK_INTERVAL_MS);
    alertCooldownMs = clampULong(alertCooldownMs, MIN_PREDATOR_ALERT_COOLDOWN_MS, MAX_PREDATOR_ALERT_COOLDOWN_MS);
    buzzerDurationMs = clampULong(buzzerDurationMs, MIN_PREDATOR_BUZZER_DURATION_MS, MAX_PREDATOR_BUZZER_DURATION_MS);
    buzzerDuty = clampInt(buzzerDuty, MIN_BUZZER_DUTY, MAX_BUZZER_DUTY);

    if (PIN_BUZZER >= 0)
    {
        // A camera esp32-camera usa LEDC_CHANNEL_0/LEDC_TIMER_0 para XCLK.
        // Usar canal explicito evita que ledcAttach() escolha automaticamente
        // o mesmo canal e cause PANIC apos a inicializacao da OV5640.
        buzzerAttached = ledcAttachChannel(PIN_BUZZER, BUZZER_PWM_FREQ_HZ, BUZZER_PWM_RESOLUTION_BITS, BUZZER_PWM_CHANNEL);
        if (buzzerAttached)
        {
            ledcWrite(PIN_BUZZER, 0);
        }
        else
        {
            Serial.printf("[PREDADORES][BUZZER][WARN] Falha ao anexar PWM gpio=%d canal=%d; PIR continua ativo sem buzzer.\n", PIN_BUZZER, BUZZER_PWM_CHANNEL);
        }
    }

    lastMotionState = readMotion();
    lastReading = readPredatorSensor("boot", false);
    fillPredatorMetadata(&lastReading);

    Serial.printf("[PREDADORES][CFG] pir_gpio=%d buzzer_gpio=%d pwm_freq=%d pwm_bits=%d pwm_channel=%d duty=%d attached=%s check_ms=%lu cooldown_ms=%lu duracao_ms=%lu upload_global=%d upload_predadores=%d\n",
                  PIN_PIR,
                  PIN_BUZZER,
                  BUZZER_PWM_FREQ_HZ,
                  BUZZER_PWM_RESOLUTION_BITS,
                  BUZZER_PWM_CHANNEL,
                  buzzerDuty,
                  buzzerAttached ? "true" : "false",
                  checkIntervalMs,
                  alertCooldownMs,
                  buzzerDurationMs,
                  NETWORK_UPLOADS_ENABLED,
                  PREDATOR_UPLOAD_ENABLED);
}

void processPredatorMonitoring()
{
    stopBuzzerIfDue();

    if (!elapsedSince(lastCheckMs, checkIntervalMs))
        return;
    lastCheckMs = millis();

    PredatorReading reading = readPredatorSensor("periodic", false);
    const bool risingEdge = reading.motionDetected && !lastMotionState;
    const bool cooldownElapsed = lastAlertMs == 0 || elapsedSince(lastAlertMs, alertCooldownMs);

    if (monitoringEnabled && reading.motionDetected && (risingEdge || cooldownElapsed))
    {
        lastAlertMs = millis();
        reading.alertEvent = true;
        reading.reason = risingEdge ? "motion_detected" : "motion_still_active";
        startBuzzer(reading.reason);
        fillPredatorMetadata(&reading);
        Serial.printf("[PREDADORES][ALERTA] Movimento detectado PIR=%d motivo=%s\n", PIN_PIR, reading.reason);
        postPredatorReading(reading);
    }

    lastMotionState = reading.motionDetected;
    lastReading = reading;
}

bool handlePredatorCommand(JsonObject command)
{
    const char *action = command["comando"] | "";

    if (strcmp(action, "configurar_predadores") == 0 || strcmp(action, "predadores_config") == 0)
    {
        updatePredatorConfigFromJson(command);
        PredatorReading reading = readPredatorSensor("config_updated", true);
        fillPredatorMetadata(&reading);
        lastReading = reading;
        postPredatorReading(lastReading);
        return true;
    }

    if (strcmp(action, "silenciar_predadores") == 0 || strcmp(action, "silenciar_alarme") == 0 || strcmp(action, "predator_silence") == 0)
    {
        stopBuzzer("manual_silence");
        PredatorReading reading = readPredatorSensor("manual_silence", true);
        fillPredatorMetadata(&reading);
        lastReading = reading;
        postPredatorReading(lastReading);
        return true;
    }

    if (strcmp(action, "testar_buzzer") == 0 || strcmp(action, "buzzer") == 0 || strcmp(action, "predator_buzzer_test") == 0)
    {
        const bool status = command["status"] | true;
        if (status)
        {
            startBuzzer("manual_test");
        }
        else
        {
            stopBuzzer("manual_test_off");
        }
        PredatorReading reading = readPredatorSensor(status ? "manual_buzzer_test" : "manual_buzzer_off", true);
        fillPredatorMetadata(&reading);
        lastReading = reading;
        postPredatorReading(lastReading);
        return true;
    }

    return false;
}

void appendPredatorTelemetry(JsonDocument &doc)
{
    doc["movimento"] = lastReading.motionDetected;
    JsonObject predadores = doc["predadores"].to<JsonObject>();
    predadores["movimento"] = lastReading.motionDetected;
    predadores["monitoramento_habilitado"] = monitoringEnabled;
    predadores["buzzer_habilitado"] = buzzerEnabled;
    predadores["alarme_ativo"] = lastReading.alarmActive;
    predadores["motivo"] = lastReading.reason;
    predadores["pir_gpio"] = PIN_PIR;
    predadores["buzzer_gpio"] = PIN_BUZZER;
    predadores["buzzer_pwm_freq_hz"] = BUZZER_PWM_FREQ_HZ;
    predadores["buzzer_pwm_resolution_bits"] = BUZZER_PWM_RESOLUTION_BITS;
    predadores["buzzer_pwm_duty"] = buzzerDuty;
    predadores["intervalo_verificacao_ms"] = checkIntervalMs;
    predadores["cooldown_alerta_ms"] = alertCooldownMs;
    predadores["duracao_buzzer_ms"] = buzzerDurationMs;
}
