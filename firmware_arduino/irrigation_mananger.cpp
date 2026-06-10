#include "irrigation_manager.h"
#include "actuators.h"
#include "config.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstring>

#ifndef IRRIGATION_INGEST_URL
#define IRRIGATION_INGEST_URL ""
#endif
#ifndef IRRIGATION_UPLOAD_TOKEN
#define IRRIGATION_UPLOAD_TOKEN CAMERA_UPLOAD_TOKEN
#endif
#ifndef IRRIGATION_INTERVAL_MS
#define IRRIGATION_INTERVAL_MS 15000UL
#endif
#ifndef IRRIGATION_PUMP_TIMEOUT_MS
#define IRRIGATION_PUMP_TIMEOUT_MS 15000UL
#endif
#ifndef SOIL_MIN_MOISTURE_PERCENT
#define SOIL_MIN_MOISTURE_PERCENT 35.0f
#endif
#ifndef SOIL_DRY_RAW
#define SOIL_DRY_RAW 3200
#endif
#ifndef SOIL_WET_RAW
#define SOIL_WET_RAW 1400
#endif
#ifndef IRRIGATION_HTTP_TIMEOUT_MS
#define IRRIGATION_HTTP_TIMEOUT_MS 12000UL
#endif
#ifndef IRRIGATION_POST_UPLOAD_SETTLE_MS
#define IRRIGATION_POST_UPLOAD_SETTLE_MS 100UL
#endif

namespace
{
    constexpr unsigned long MIN_IRRIGATION_INTERVAL_MS = 5000UL;
    constexpr unsigned long MAX_IRRIGATION_INTERVAL_MS = 3600000UL;
    constexpr unsigned long MIN_PUMP_TIMEOUT_MS = 1000UL;
    constexpr unsigned long MAX_PUMP_TIMEOUT_MS = 60000UL;
    constexpr float MIN_SOIL_THRESHOLD_PERCENT = 1.0f;
    constexpr float MAX_SOIL_THRESHOLD_PERCENT = 95.0f;

    struct IrrigationReading
    {
        bool valid;
        int soilRaw;
        float soilMoisturePercent;
        bool lowSoilMoisture;
        bool pumpOn;
        bool autoPumpTriggered;
        bool pumpEvent;
        const char *pumpReason;
        float minMoisturePercent;
        unsigned long readIntervalMs;
        unsigned long pumpTimeoutMs;
        int soilDryRaw;
        int soilWetRaw;
    };

    float minMoisturePercent = SOIL_MIN_MOISTURE_PERCENT;
    unsigned long readIntervalMs = IRRIGATION_INTERVAL_MS;
    unsigned long pumpTimeoutMs = IRRIGATION_PUMP_TIMEOUT_MS;
    int soilDryRaw = SOIL_DRY_RAW;
    int soilWetRaw = SOIL_WET_RAW;
    unsigned long lastSoilReadMs = 0;
    unsigned long pumpAutoOffAtMs = 0;

    IrrigationReading lastReading = {
        false,
        0,
        0.0f,
        false,
        false,
        false,
        false,
        "boot",
        SOIL_MIN_MOISTURE_PERCENT,
        IRRIGATION_INTERVAL_MS,
        IRRIGATION_PUMP_TIMEOUT_MS,
        SOIL_DRY_RAW,
        SOIL_WET_RAW,
    };

    void irrigationYield()
    {
        yield();
        delay(1);
    }

    float clampFloat(float value, float minValue, float maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
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

    float soilPercentFromRaw(int raw)
    {
        if (soilDryRaw == soilWetRaw)
            return 0.0f;

        float percent;
        if (soilDryRaw > soilWetRaw)
        {
            percent = (static_cast<float>(soilDryRaw - raw) * 100.0f) / static_cast<float>(soilDryRaw - soilWetRaw);
        }
        else
        {
            percent = (static_cast<float>(raw - soilDryRaw) * 100.0f) / static_cast<float>(soilWetRaw - soilDryRaw);
        }
        return clampFloat(percent, 0.0f, 100.0f);
    }

    void fillIrrigationMetadata(IrrigationReading *reading)
    {
        reading->pumpOn = isPumpOn();
        reading->minMoisturePercent = minMoisturePercent;
        reading->readIntervalMs = readIntervalMs;
        reading->pumpTimeoutMs = pumpTimeoutMs;
        reading->soilDryRaw = soilDryRaw;
        reading->soilWetRaw = soilWetRaw;
    }

    IrrigationReading readIrrigationSensors()
    {
        IrrigationReading reading = lastReading;
        reading.valid = true;
        reading.soilRaw = readSoilRaw();
        reading.soilMoisturePercent = soilPercentFromRaw(reading.soilRaw);
        reading.lowSoilMoisture = reading.soilMoisturePercent <= minMoisturePercent;
        reading.autoPumpTriggered = false;
        reading.pumpEvent = false;
        reading.pumpReason = "reading";
        fillIrrigationMetadata(&reading);
        return reading;
    }

    String buildIrrigationPayload(const IrrigationReading &reading)
    {
        JsonDocument doc;
        doc["deviceId"] = DEVICE_ID;
        doc["namespace"] = MQTT_NAMESPACE;
        doc["timestamp"] = nowIso8601();
        doc["uptime_ms"] = millis();
        doc["soil_raw"] = reading.soilRaw;
        doc["soil_moisture_percent"] = reading.soilMoisturePercent;
        doc["low_soil_moisture"] = reading.lowSoilMoisture;
        doc["soil_min_moisture_percent"] = reading.minMoisturePercent;
        doc["soil_dry_raw"] = reading.soilDryRaw;
        doc["soil_wet_raw"] = reading.soilWetRaw;
        doc["read_interval_ms"] = reading.readIntervalMs;
        doc["pump_on"] = reading.pumpOn;
        doc["pump_reason"] = reading.pumpReason;
        doc["pump_event"] = reading.pumpEvent;
        doc["auto_pump_triggered"] = reading.autoPumpTriggered;
        doc["pump_timeout_ms"] = reading.pumpTimeoutMs;
        doc["source"] = "esp32_s3_irrigation";

        String output;
        serializeJson(doc, output);
        return output;
    }

    bool postIrrigationReading(const IrrigationReading &reading)
    {
        if (strlen(IRRIGATION_INGEST_URL) == 0)
        {
            Serial.println("[REGA][UPLOAD][WARN] IRRIGATION_INGEST_URL vazio; leitura nao enviada ao Firebase.");
            return false;
        }
        if (!isWiFiConnected())
        {
            Serial.println("[REGA][UPLOAD][WARN] Wi-Fi indisponivel.");
            return false;
        }

        const String payload = buildIrrigationPayload(reading);
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(IRRIGATION_HTTP_TIMEOUT_MS);
        http.useHTTP10(true);

        if (!http.begin(client, IRRIGATION_INGEST_URL))
        {
            Serial.println("[REGA][UPLOAD][ERRO] http.begin falhou.");
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-camera-upload-token", IRRIGATION_UPLOAD_TOKEN);
        http.addHeader("x-device-id", DEVICE_ID);
        http.addHeader("x-namespace", MQTT_NAMESPACE);

        Serial.printf("[REGA][UPLOAD] Enviando leitura: solo=%d umidade=%.1f%% bomba=%s motivo=%s\n",
                      reading.soilRaw,
                      reading.soilMoisturePercent,
                      reading.pumpOn ? "ON" : "OFF",
                      reading.pumpReason);

        const int status = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(payload.c_str())), payload.length());
        const String response = http.getString();
        http.end();
        client.stop();
        delay(IRRIGATION_POST_UPLOAD_SETTLE_MS);
        irrigationYield();

        Serial.printf("[REGA][UPLOAD] HTTP %d resposta=%s\n", status, response.substring(0, 160).c_str());
        return status >= 200 && status < 300;
    }

    void publishPumpTimeoutIfNeeded()
    {
        if (!isPumpOn() || pumpAutoOffAtMs == 0)
            return;
        if (static_cast<long>(millis() - pumpAutoOffAtMs) < 0)
            return;

        setPump(false);
        pumpAutoOffAtMs = 0;

        IrrigationReading reading = readIrrigationSensors();
        reading.pumpOn = isPumpOn();
        reading.pumpReason = "timeout_off";
        reading.pumpEvent = true;
        reading.autoPumpTriggered = false;
        fillIrrigationMetadata(&reading);
        lastReading = reading;
        postIrrigationReading(lastReading);
    }

    void applyIrrigationAutomation(IrrigationReading *reading)
    {
        reading->autoPumpTriggered = false;
        reading->pumpEvent = false;

        if (reading->lowSoilMoisture)
        {
            if (!isPumpOn())
            {
                Serial.printf("[REGA][AUTO] Solo seco %.1f%% <= %.1f%%. Ligando bomba por %lu ms.\n",
                              reading->soilMoisturePercent,
                              minMoisturePercent,
                              pumpTimeoutMs);
                setPump(true);
                pumpAutoOffAtMs = millis() + pumpTimeoutMs;
                reading->autoPumpTriggered = true;
                reading->pumpEvent = true;
            }
            reading->pumpReason = "auto_dry_soil";
        }
        else
        {
            reading->pumpReason = isPumpOn() ? "pump_on_until_timeout" : "soil_ok";
        }

        fillIrrigationMetadata(reading);
    }

    float jsonFloatOr(JsonObject command, const char *a, const char *b, float fallback)
    {
        if (command[a].is<float>() || command[a].is<int>())
            return command[a].as<float>();
        if (command[b].is<float>() || command[b].is<int>())
            return command[b].as<float>();
        return fallback;
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

    void updateIrrigationConfigFromJson(JsonObject command)
    {
        const float threshold = jsonFloatOr(command, "soil_min_moisture_percent", "umidade_minima_solo_percent", minMoisturePercent);
        const unsigned long intervalMs = jsonULongOr(command, "soil_read_interval_ms", "intervalo_leitura_solo_ms", readIntervalMs);
        const unsigned long timeoutMs = jsonULongOr(command, "pump_timeout_ms", "timeout_bomba_ms", pumpTimeoutMs);
        const int dryRaw = jsonIntOr(command, "soil_dry_raw", "solo_seco_raw", soilDryRaw);
        const int wetRaw = jsonIntOr(command, "soil_wet_raw", "solo_umido_raw", soilWetRaw);

        minMoisturePercent = clampFloat(threshold, MIN_SOIL_THRESHOLD_PERCENT, MAX_SOIL_THRESHOLD_PERCENT);
        readIntervalMs = clampULong(intervalMs, MIN_IRRIGATION_INTERVAL_MS, MAX_IRRIGATION_INTERVAL_MS);
        pumpTimeoutMs = clampULong(timeoutMs, MIN_PUMP_TIMEOUT_MS, MAX_PUMP_TIMEOUT_MS);
        soilDryRaw = clampInt(dryRaw, 0, 4095);
        soilWetRaw = clampInt(wetRaw, 0, 4095);
        if (soilDryRaw == soilWetRaw)
            soilWetRaw = soilDryRaw > 0 ? soilDryRaw - 1 : soilDryRaw + 1;

        Serial.printf("[REGA][CFG] min=%.1f%% interval_ms=%lu timeout_ms=%lu dry_raw=%d wet_raw=%d\n",
                      minMoisturePercent,
                      readIntervalMs,
                      pumpTimeoutMs,
                      soilDryRaw,
                      soilWetRaw);

        lastReading.pumpReason = "config_updated";
        fillIrrigationMetadata(&lastReading);
    }
}

void setupIrrigationManager()
{
    readIntervalMs = clampULong(readIntervalMs, MIN_IRRIGATION_INTERVAL_MS, MAX_IRRIGATION_INTERVAL_MS);
    pumpTimeoutMs = clampULong(pumpTimeoutMs, MIN_PUMP_TIMEOUT_MS, MAX_PUMP_TIMEOUT_MS);
    minMoisturePercent = clampFloat(minMoisturePercent, MIN_SOIL_THRESHOLD_PERCENT, MAX_SOIL_THRESHOLD_PERCENT);

    lastReading = readIrrigationSensors();
    lastReading.pumpReason = "boot";
    fillIrrigationMetadata(&lastReading);

    Serial.printf("[REGA][CFG] solo_gpio=%d bomba_gpio=%d interval_ms=%lu min=%.1f%% timeout_ms=%lu dry_raw=%d wet_raw=%d\n",
                  PIN_SOLO_ADC,
                  PIN_RELE_BOMBA,
                  readIntervalMs,
                  minMoisturePercent,
                  pumpTimeoutMs,
                  soilDryRaw,
                  soilWetRaw);
}

void processIrrigationAutomation()
{
    publishPumpTimeoutIfNeeded();

    if (millis() - lastSoilReadMs < readIntervalMs)
        return;
    lastSoilReadMs = millis();

    IrrigationReading reading = readIrrigationSensors();
    applyIrrigationAutomation(&reading);
    lastReading = reading;

    Serial.printf("[REGA] solo=%d umidade=%.1f%% minimo=%.1f%% bomba=%s motivo=%s\n",
                  lastReading.soilRaw,
                  lastReading.soilMoisturePercent,
                  minMoisturePercent,
                  lastReading.pumpOn ? "ON" : "OFF",
                  lastReading.pumpReason);

    postIrrigationReading(lastReading);
}

bool handleIrrigationCommand(JsonObject command)
{
    const char *action = command["comando"] | "";
    const bool status = command["status"] | true;

    if (strcmp(action, "configurar_rega") == 0 || strcmp(action, "rega_config") == 0)
    {
        updateIrrigationConfigFromJson(command);
        lastReading.pumpEvent = true;
        postIrrigationReading(lastReading);
        return true;
    }

    if (strcmp(action, "irrigar") == 0 || strcmp(action, "bomba") == 0 || strcmp(action, "pump") == 0)
    {
        setPump(status);
        pumpAutoOffAtMs = status ? millis() + pumpTimeoutMs : 0;

        IrrigationReading commandReading = readIrrigationSensors();
        commandReading.pumpOn = isPumpOn();
        commandReading.autoPumpTriggered = false;
        commandReading.pumpEvent = true;
        commandReading.pumpReason = status ? "manual_on" : "manual_off";
        fillIrrigationMetadata(&commandReading);
        lastReading = commandReading;

        Serial.printf("[REGA][CMD] Bomba manual=%s timeout_ms=%lu\n",
                      status ? "ON" : "OFF",
                      pumpTimeoutMs);
        postIrrigationReading(lastReading);
        return true;
    }

    return false;
}

void appendIrrigationTelemetry(JsonDocument &doc)
{
    doc["solo_raw"] = lastReading.soilRaw;
    JsonObject solo = doc["solo"].to<JsonObject>();
    solo["raw"] = lastReading.soilRaw;
    solo["umidade_percentual"] = lastReading.soilMoisturePercent;
    solo["umidade_minima_percentual"] = minMoisturePercent;
    solo["baixo"] = lastReading.lowSoilMoisture;
    solo["intervalo_leitura_ms"] = readIntervalMs;
    solo["seco_raw"] = soilDryRaw;
    solo["umido_raw"] = soilWetRaw;

    JsonObject bomba = doc["bomba"].to<JsonObject>();
    bomba["ligada"] = lastReading.pumpOn;
    bomba["motivo"] = lastReading.pumpReason;
    bomba["timeout_ms"] = pumpTimeoutMs;
    bomba["acionamento_automatico"] = lastReading.autoPumpTriggered;
}
