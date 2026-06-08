#include "climate_manager.h"
#include "actuators.h"
#include "config.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <cstring>
#include <math.h>

#ifndef CLIMATE_INGEST_URL
#define CLIMATE_INGEST_URL ""
#endif
#ifndef CLIMATE_UPLOAD_TOKEN
#define CLIMATE_UPLOAD_TOKEN CAMERA_UPLOAD_TOKEN
#endif
#ifndef HDC1080_SDA_PIN
#define HDC1080_SDA_PIN 14
#endif
#ifndef HDC1080_SCL_PIN
#define HDC1080_SCL_PIN 21
#endif
#ifndef HDC1080_I2C_ADDRESS
#define HDC1080_I2C_ADDRESS 0x40
#endif
#ifndef HDC1080_I2C_FREQUENCY_HZ
#define HDC1080_I2C_FREQUENCY_HZ 100000UL
#endif
#ifndef CLIMATE_INTERVAL_MS
#define CLIMATE_INTERVAL_MS 60000UL
#endif
#ifndef LDR_DARK_THRESHOLD_RAW
#define LDR_DARK_THRESHOLD_RAW 1200
#endif
#ifndef LDR_LIGHT_HYSTERESIS_RAW
#define LDR_LIGHT_HYSTERESIS_RAW 250
#endif
#ifndef CLIMATE_MANUAL_LIGHT_OVERRIDE_MS
#define CLIMATE_MANUAL_LIGHT_OVERRIDE_MS 1800000UL
#endif
#ifndef CLIMATE_HTTP_TIMEOUT_MS
#define CLIMATE_HTTP_TIMEOUT_MS 15000UL
#endif
#ifndef LDR_ADC_MAX_VALUE
#define LDR_ADC_MAX_VALUE 4095.0f
#endif

namespace
{
    constexpr uint8_t HDC1080_REG_TEMPERATURE = 0x00;
    constexpr uint8_t HDC1080_REG_HUMIDITY = 0x01;

    ClimateReading lastReading = {
        false,
        0,
        0.0f,
        false,
        NAN,
        NAN,
        false,
        false,
        false,
        "boot",
    };

    unsigned long lastClimateReadMs = 0;
    unsigned long manualLightOverrideUntilMs = 0;
    bool manualOverrideActive = false;
    bool hdcOnline = false;

    float ldrPercentFromRaw(int raw)
    {
        if (raw <= 0)
            return 0.0f;
        if (raw >= static_cast<int>(LDR_ADC_MAX_VALUE))
            return 100.0f;
        return (static_cast<float>(raw) * 100.0f) / LDR_ADC_MAX_VALUE;
    }

    bool isManualOverrideActive()
    {
        if (!manualOverrideActive)
            return false;
        if (millis() <= manualLightOverrideUntilMs)
            return true;
        manualOverrideActive = false;
        Serial.println("[CLIMA] Override manual da iluminacao expirado; automacao retomada.");
        return false;
    }

    bool readHdc1080Register(uint8_t reg, uint16_t *rawValue)
    {
        Wire.beginTransmission(HDC1080_I2C_ADDRESS);
        Wire.write(reg);
        if (Wire.endTransmission() != 0)
            return false;

        delay(20);
        const size_t bytesRead = Wire.requestFrom(static_cast<uint8_t>(HDC1080_I2C_ADDRESS), static_cast<uint8_t>(2));
        if (bytesRead != 2)
            return false;

        const uint16_t msb = Wire.read();
        const uint16_t lsb = Wire.read();
        *rawValue = static_cast<uint16_t>((msb << 8) | lsb);
        return true;
    }

    bool readHdc1080(float *temperatureC, float *humidityPercent)
    {
        uint16_t rawTemp = 0;
        uint16_t rawHumidity = 0;
        if (!readHdc1080Register(HDC1080_REG_TEMPERATURE, &rawTemp))
            return false;
        if (!readHdc1080Register(HDC1080_REG_HUMIDITY, &rawHumidity))
            return false;

        *temperatureC = ((static_cast<float>(rawTemp) / 65536.0f) * 165.0f) - 40.0f;
        *humidityPercent = (static_cast<float>(rawHumidity) / 65536.0f) * 100.0f;
        return true;
    }

    void applyLightAutomation(ClimateReading *reading)
    {
        reading->autoLightTriggered = false;

        if (isManualOverrideActive())
        {
            reading->lampOn = isLampOn();
            reading->lampReason = "manual_override";
            return;
        }

        if (reading->ldrRaw <= LDR_DARK_THRESHOLD_RAW)
        {
            if (!isLampOn())
            {
                Serial.printf("[CLIMA][LUZ] Baixa luminosidade LDR=%d <= %d. Acendendo lampada LED.\n",
                              reading->ldrRaw,
                              LDR_DARK_THRESHOLD_RAW);
                setLamp(true);
                reading->autoLightTriggered = true;
            }
            reading->lampReason = "auto_low_light";
        }
        else if (reading->ldrRaw >= LDR_DARK_THRESHOLD_RAW + LDR_LIGHT_HYSTERESIS_RAW)
        {
            if (isLampOn())
            {
                Serial.printf("[CLIMA][LUZ] Luminosidade recuperada LDR=%d >= %d. Apagando lampada LED.\n",
                              reading->ldrRaw,
                              LDR_DARK_THRESHOLD_RAW + LDR_LIGHT_HYSTERESIS_RAW);
                setLamp(false);
            }
            reading->lampReason = "auto_light_ok";
        }
        else
        {
            reading->lampReason = "auto_hysteresis";
        }

        reading->lampOn = isLampOn();
    }

    String buildClimatePayload(const ClimateReading &reading)
    {
        JsonDocument doc;
        doc["deviceId"] = DEVICE_ID;
        doc["namespace"] = MQTT_NAMESPACE;
        doc["timestamp"] = nowIso8601();
        doc["uptime_ms"] = millis();
        doc["ldr_raw"] = reading.ldrRaw;
        doc["ldr_percent"] = reading.ldrPercent;
        doc["low_light"] = reading.lowLight;
        doc["auto_light_triggered"] = reading.autoLightTriggered;
        doc["light_threshold_raw"] = LDR_DARK_THRESHOLD_RAW;
        doc["light_hysteresis_raw"] = LDR_LIGHT_HYSTERESIS_RAW;
        doc["lamp_on"] = reading.lampOn;
        doc["lamp_reason"] = reading.lampReason;
        doc["manual_override_active"] = isManualOverrideActive();
        doc["hdc1080_available"] = reading.hdcAvailable;
        if (reading.hdcAvailable)
        {
            doc["temp_c"] = reading.temperatureC;
            doc["humidity_percent"] = reading.humidityPercent;
        }

        String output;
        serializeJson(doc, output);
        return output;
    }

    bool postClimateReading(const ClimateReading &reading)
    {
        if (strlen(CLIMATE_INGEST_URL) == 0)
        {
            Serial.println("[CLIMA][UPLOAD][WARN] CLIMATE_INGEST_URL vazio; leitura nao enviada ao Firebase.");
            return false;
        }
        if (!isWiFiConnected())
        {
            Serial.println("[CLIMA][UPLOAD][WARN] Wi-Fi indisponivel.");
            return false;
        }

        const String payload = buildClimatePayload(reading);
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(CLIMATE_HTTP_TIMEOUT_MS);
        http.useHTTP10(true);

        if (!http.begin(client, CLIMATE_INGEST_URL))
        {
            Serial.println("[CLIMA][UPLOAD][ERRO] http.begin falhou.");
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-camera-upload-token", CLIMATE_UPLOAD_TOKEN);
        http.addHeader("x-device-id", DEVICE_ID);
        http.addHeader("x-namespace", MQTT_NAMESPACE);

        Serial.printf("[CLIMA][UPLOAD] Enviando leitura: ldr=%d temp=%.2f umid=%.2f lamp=%s\n",
                      reading.ldrRaw,
                      reading.temperatureC,
                      reading.humidityPercent,
                      reading.lampOn ? "ON" : "OFF");

        const int status = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(payload.c_str())), payload.length());
        const String response = http.getString();
        http.end();
        client.stop();

        Serial.printf("[CLIMA][UPLOAD] HTTP %d resposta=%s\n", status, response.substring(0, 160).c_str());
        return status >= 200 && status < 300;
    }

    ClimateReading readClimateSensors()
    {
        ClimateReading reading = lastReading;
        reading.valid = true;
        reading.ldrRaw = readLdrRaw();
        reading.ldrPercent = ldrPercentFromRaw(reading.ldrRaw);
        reading.lowLight = reading.ldrRaw <= LDR_DARK_THRESHOLD_RAW;

        float temperatureC = NAN;
        float humidityPercent = NAN;
        reading.hdcAvailable = readHdc1080(&temperatureC, &humidityPercent);
        hdcOnline = reading.hdcAvailable;
        if (reading.hdcAvailable)
        {
            reading.temperatureC = temperatureC;
            reading.humidityPercent = humidityPercent;
        }

        applyLightAutomation(&reading);
        return reading;
    }
}

void setupClimateManager()
{
    Wire.begin(HDC1080_SDA_PIN, HDC1080_SCL_PIN, HDC1080_I2C_FREQUENCY_HZ);
    Wire.setTimeOut(100);

    float temperatureC = NAN;
    float humidityPercent = NAN;
    hdcOnline = readHdc1080(&temperatureC, &humidityPercent);

    Serial.printf("[CLIMA][CFG] ldr_gpio=%d adc=12bits threshold=%d hysteresis=%d lamp_gpio=%d hdc1080=%s sda=%d scl=%d addr=0x%02x interval_ms=%lu\n",
                  PIN_LDR_ADC,
                  LDR_DARK_THRESHOLD_RAW,
                  LDR_LIGHT_HYSTERESIS_RAW,
                  PIN_RELE_LAMPADA,
                  hdcOnline ? "OK" : "NAO_DETECTADO",
                  HDC1080_SDA_PIN,
                  HDC1080_SCL_PIN,
                  HDC1080_I2C_ADDRESS,
                  static_cast<unsigned long>(CLIMATE_INTERVAL_MS));

    if (hdcOnline)
    {
        lastReading.hdcAvailable = true;
        lastReading.temperatureC = temperatureC;
        lastReading.humidityPercent = humidityPercent;
    }
}

void processClimateAutomation()
{
    if (millis() - lastClimateReadMs < CLIMATE_INTERVAL_MS)
        return;
    lastClimateReadMs = millis();

    lastReading = readClimateSensors();
    Serial.printf("[CLIMA] ldr=%d(%.1f%%) temp=%.2fC umid=%.2f%% hdc=%s lamp=%s motivo=%s\n",
                  lastReading.ldrRaw,
                  lastReading.ldrPercent,
                  lastReading.temperatureC,
                  lastReading.humidityPercent,
                  lastReading.hdcAvailable ? "OK" : "FALHA",
                  lastReading.lampOn ? "ON" : "OFF",
                  lastReading.lampReason);

    postClimateReading(lastReading);
}

bool handleClimateCommand(JsonObject command)
{
    const char *action = command["comando"] | "";
    const bool status = command["status"] | true;

    if (strcmp(action, "iluminar") != 0 &&
        strcmp(action, "lampada") != 0 &&
        strcmp(action, "luz") != 0 &&
        strcmp(action, "aquecer") != 0)
    {
        return false;
    }

    manualOverrideActive = true;
    manualLightOverrideUntilMs = millis() + CLIMATE_MANUAL_LIGHT_OVERRIDE_MS;
    setLamp(status);
    lastReading.lampOn = isLampOn();
    lastReading.autoLightTriggered = false;
    lastReading.lampReason = status ? "manual_on" : "manual_off";

    Serial.printf("[CLIMA][CMD] Iluminacao manual=%s override_ms=%lu\n",
                  status ? "ON" : "OFF",
                  static_cast<unsigned long>(CLIMATE_MANUAL_LIGHT_OVERRIDE_MS));

    ClimateReading commandReading = readClimateSensors();
    commandReading.lampOn = isLampOn();
    commandReading.autoLightTriggered = false;
    commandReading.lampReason = status ? "manual_on" : "manual_off";
    lastReading = commandReading;
    postClimateReading(lastReading);
    return true;
}

void appendClimateTelemetry(JsonDocument &doc)
{
    doc["ldr_raw"] = lastReading.ldrRaw;
    JsonObject luminosidade = doc["luminosidade"].to<JsonObject>();
    luminosidade["raw"] = lastReading.ldrRaw;
    luminosidade["percentual"] = lastReading.ldrPercent;
    luminosidade["limite_minimo_raw"] = LDR_DARK_THRESHOLD_RAW;
    luminosidade["baixa"] = lastReading.lowLight;
    luminosidade["acionamento_automatico_lampada"] = lastReading.autoLightTriggered;

    JsonObject clima = doc["clima"].to<JsonObject>();
    clima["hdc1080_disponivel"] = lastReading.hdcAvailable;
    if (lastReading.hdcAvailable)
    {
        clima["temp_c"] = lastReading.temperatureC;
        clima["umidade_ar"] = lastReading.humidityPercent;
    }

    JsonObject iluminacao = doc["iluminacao"].to<JsonObject>();
    iluminacao["lampada_on"] = isLampOn();
    iluminacao["motivo"] = lastReading.lampReason;
    iluminacao["override_manual"] = isManualOverrideActive();
}

ClimateReading getLastClimateReading()
{
    return lastReading;
}
