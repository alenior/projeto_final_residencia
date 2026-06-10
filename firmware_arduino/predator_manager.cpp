#include "predator_manager.h"
#include "actuators.h"
#include "config.h"
#include "sd_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
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
        Serial.printf("[PREDADORES][BUZZER] ON motivo=%s freq=%d duty=%d duracao_ms=%lu\n",
                      reason,
                      BUZZER_PWM_FREQ_HZ,
                      buzzerDuty,
                      buzzerDurationMs);
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

    bool postPredatorReading(const PredatorReading &reading)
    {
        const String payload = buildPredatorPayload(reading);
        sdAppendLogJson("predadores", payload);

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

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(PREDATOR_HTTP_TIMEOUT_MS);
        http.useHTTP10(true);

        if (!http.begin(client, PREDATOR_INGEST_URL))
        {
            Serial.println("[PREDADORES][UPLOAD][ERRO] http.begin falhou.");
            sdQueueFirebaseJson("predadores", PREDATOR_INGEST_URL, PREDATOR_UPLOAD_TOKEN, payload);
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-camera-upload-token", PREDATOR_UPLOAD_TOKEN);
        http.addHeader("x-device-id", DEVICE_ID);
        http.addHeader("x-namespace", MQTT_NAMESPACE);

        Serial.printf("[PREDADORES][UPLOAD] Enviando evento: movimento=%s alarme=%s motivo=%s\n",
                      reading.motionDetected ? "true" : "false",
                      reading.alarmActive ? "true" : "false",
                      reading.reason);

        const int status = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(payload.c_str())), payload.length());
        const String response = http.getString();
        http.end();
        client.stop();
        delay(PREDATOR_POST_UPLOAD_SETTLE_MS);
        predatorYield();

        const bool ok = status >= 200 && status < 300;
        Serial.printf("[PREDADORES][UPLOAD] HTTP %d resposta=%s\n", status, response.substring(0, 160).c_str());
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
        buzzerAttached = ledcAttach(PIN_BUZZER, BUZZER_PWM_FREQ_HZ, BUZZER_PWM_RESOLUTION_BITS);
        ledcWrite(PIN_BUZZER, 0);
    }

    lastMotionState = readMotion();
    lastReading = readPredatorSensor("boot", false);
    fillPredatorMetadata(&lastReading);

    Serial.printf("[PREDADORES][CFG] pir_gpio=%d buzzer_gpio=%d pwm_freq=%d pwm_bits=%d duty=%d attached=%s check_ms=%lu cooldown_ms=%lu duracao_ms=%lu\n",
                  PIN_PIR,
                  PIN_BUZZER,
                  BUZZER_PWM_FREQ_HZ,
                  BUZZER_PWM_RESOLUTION_BITS,
                  buzzerDuty,
                  buzzerAttached ? "true" : "false",
                  checkIntervalMs,
                  alertCooldownMs,
                  buzzerDurationMs);
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
