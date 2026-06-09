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
#ifndef CLIMATE_FAN_TEMP_THRESHOLD_C
#define CLIMATE_FAN_TEMP_THRESHOLD_C 35.0f
#endif
#ifndef CLIMATE_FAN_CHECK_INTERVAL_MS
#define CLIMATE_FAN_CHECK_INTERVAL_MS 300000UL
#endif
#ifndef CLIMATE_FAN_TIMEOUT_MS
#define CLIMATE_FAN_TIMEOUT_MS 30000UL
#endif
#ifndef CLIMATE_HTTP_TIMEOUT_MS
#define CLIMATE_HTTP_TIMEOUT_MS 15000UL
#endif
#ifndef CLIMATE_UPLOAD_USE_HTTPCLIENT
#define CLIMATE_UPLOAD_USE_HTTPCLIENT false
#endif
#ifndef CLIMATE_UPLOAD_CHUNK_BYTES
#define CLIMATE_UPLOAD_CHUNK_BYTES 256UL
#endif
#ifndef CLIMATE_POST_UPLOAD_SETTLE_MS
#define CLIMATE_POST_UPLOAD_SETTLE_MS 150UL
#endif
#ifndef LDR_ADC_MAX_VALUE
#define LDR_ADC_MAX_VALUE 4095.0f
#endif

namespace
{
    constexpr uint8_t HDC1080_REG_TEMPERATURE = 0x00;
    constexpr uint8_t HDC1080_REG_HUMIDITY = 0x01;
    constexpr unsigned long MIN_FAN_CHECK_INTERVAL_MS = 30000UL;
    constexpr unsigned long MIN_FAN_TIMEOUT_MS = 5000UL;
    constexpr unsigned long MAX_FAN_TIMEOUT_MS = 300000UL;

    struct ClimateRuntimeReading
    {
        bool valid;
        int ldrRaw;
        float ldrPercent;
        bool hdcAvailable;
        float temperatureC;
        float humidityPercent;
        bool lampOn;
        bool fanOn;
        bool lowLight;
        bool autoLightTriggered;
        bool autoFanTriggered;
        bool fanEvent;
        const char *lampReason;
        const char *fanReason;
        float fanTempThresholdC;
        unsigned long fanCheckIntervalMs;
        unsigned long fanTimeoutMs;
    };

    float fanTempThresholdC = CLIMATE_FAN_TEMP_THRESHOLD_C;
    unsigned long fanCheckIntervalMs = CLIMATE_FAN_CHECK_INTERVAL_MS;
    unsigned long fanTimeoutMs = CLIMATE_FAN_TIMEOUT_MS;
    unsigned long fanAutoOffAtMs = 0;
    unsigned long lastFanCheckMs = 0;

    ClimateRuntimeReading lastReading = {
        false,
        0,
        0.0f,
        false,
        NAN,
        NAN,
        false,
        false,
        false,
        false,
        false,
        false,
        "boot",
        "boot",
        CLIMATE_FAN_TEMP_THRESHOLD_C,
        CLIMATE_FAN_CHECK_INTERVAL_MS,
        CLIMATE_FAN_TIMEOUT_MS,
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

    void fillFanMetadata(ClimateRuntimeReading *reading)
    {
        reading->fanOn = isFanOn();
        reading->fanTempThresholdC = fanTempThresholdC;
        reading->fanCheckIntervalMs = fanCheckIntervalMs;
        reading->fanTimeoutMs = fanTimeoutMs;
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

    void applyLightAutomation(ClimateRuntimeReading *reading)
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

    void scheduleFanTimeout(unsigned long now)
    {
        fanAutoOffAtMs = now + fanTimeoutMs;
    }

    void applyFanAutomation(ClimateRuntimeReading *reading, bool forceCheck = false)
    {
        fillFanMetadata(reading);
        reading->autoFanTriggered = false;
        reading->fanEvent = false;

        if (!reading->hdcAvailable)
        {
            reading->fanReason = isFanOn() ? "sensor_unavailable_keep_state" : "sensor_unavailable";
            return;
        }

        const unsigned long now = millis();
        if (!forceCheck && now - lastFanCheckMs < fanCheckIntervalMs)
        {
            reading->fanReason = isFanOn() ? "waiting_timeout" : "waiting_next_check";
            return;
        }

        lastFanCheckMs = now;
        if (reading->temperatureC >= fanTempThresholdC)
        {
            if (!isFanOn())
            {
                Serial.printf("[CLIMA][VENTOINHA] Temperatura %.2fC >= %.2fC. Ligando ventoinha por %lu ms.\n",
                              reading->temperatureC,
                              fanTempThresholdC,
                              fanTimeoutMs);
                setFan(true);
                reading->autoFanTriggered = true;
                reading->fanEvent = true;
            }
            scheduleFanTimeout(now);
            reading->fanReason = "auto_high_temperature";
        }
        else
        {
            reading->fanReason = isFanOn() ? "fan_on_until_timeout" : "temperature_ok";
        }

        fillFanMetadata(reading);
    }

    String buildClimatePayload(const ClimateRuntimeReading &reading)
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
        doc["fan_on"] = reading.fanOn;
        doc["fan_reason"] = reading.fanReason;
        doc["auto_fan_triggered"] = reading.autoFanTriggered;
        doc["fan_event"] = reading.fanEvent;
        doc["fan_temp_threshold_c"] = reading.fanTempThresholdC;
        doc["fan_check_interval_ms"] = reading.fanCheckIntervalMs;
        doc["fan_timeout_ms"] = reading.fanTimeoutMs;
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

    void climateYield()
    {
        yield();
        delay(1);
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

    bool readClimateHttpsResponse(WiFiClientSecure &client, int *statusCode, String *responsePreview)
    {
        const unsigned long deadline = millis() + CLIMATE_HTTP_TIMEOUT_MS;
        while (client.connected() && !client.available() && millis() < deadline)
        {
            climateYield();
            delay(10);
        }

        if (!client.available())
        {
            *statusCode = -1;
            *responsePreview = "timeout aguardando resposta";
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

        while (client.connected())
        {
            const String line = client.readStringUntil('\n');
            if (line == "\r" || line.length() == 0)
                break;
            climateYield();
        }

        String body;
        const unsigned long bodyDeadline = millis() + 1500UL;
        while (millis() < bodyDeadline && body.length() < 160)
        {
            while (client.available() && body.length() < 160)
            {
                body += static_cast<char>(client.read());
            }
            if (!client.connected() && !client.available())
                break;
            climateYield();
            delay(5);
        }

        *statusCode = parsedStatus;
        *responsePreview = body.length() > 0 ? body : statusLine.substring(0, 160);
        return parsedStatus >= 200 && parsedStatus < 300;
    }

    bool postClimateReadingWithRawTls(const ClimateRuntimeReading &reading, const String &payload)
    {
        (void)reading;

        String host;
        String path;
        uint16_t port = 443;
        if (!parseHttpsUrl(CLIMATE_INGEST_URL, &host, &port, &path))
        {
            Serial.println("[CLIMA][UPLOAD][ERRO] CLIMATE_INGEST_URL deve iniciar com https://.");
            return false;
        }

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(CLIMATE_HTTP_TIMEOUT_MS / 1000UL);

        Serial.printf("[CLIMA][UPLOAD] TLS raw host=%s bytes=%u chunk=%lu\n",
                      host.c_str(),
                      static_cast<unsigned>(payload.length()),
                      static_cast<unsigned long>(CLIMATE_UPLOAD_CHUNK_BYTES));

        if (!client.connect(host.c_str(), port))
        {
            Serial.println("[CLIMA][UPLOAD][ERRO] Falha ao conectar TLS.");
            client.stop();
            return false;
        }

        client.printf("POST %s HTTP/1.1\r\n", path.c_str());
        client.printf("Host: %s\r\n", host.c_str());
        client.print("Content-Type: application/json\r\n");
        client.printf("Content-Length: %u\r\n", static_cast<unsigned>(payload.length()));
        client.printf("x-camera-upload-token: %s\r\n", CLIMATE_UPLOAD_TOKEN);
        client.printf("x-device-id: %s\r\n", DEVICE_ID);
        client.printf("x-namespace: %s\r\n", MQTT_NAMESPACE);
        client.print("Connection: close\r\n\r\n");

        const char *data = payload.c_str();
        size_t sent = 0;
        while (sent < payload.length())
        {
            const size_t remaining = payload.length() - sent;
            const size_t chunk = remaining < CLIMATE_UPLOAD_CHUNK_BYTES ? remaining : CLIMATE_UPLOAD_CHUNK_BYTES;
            const size_t written = client.write(reinterpret_cast<const uint8_t *>(data + sent), chunk);
            if (written == 0)
            {
                Serial.println("[CLIMA][UPLOAD][ERRO] Escrita TLS interrompida.");
                client.stop();
                return false;
            }
            sent += written;
            climateYield();
        }

        int status = -1;
        String response;
        const bool ok = readClimateHttpsResponse(client, &status, &response);
        client.stop();
        delay(CLIMATE_POST_UPLOAD_SETTLE_MS);

        Serial.printf("[CLIMA][UPLOAD] HTTP %d resposta=%s\n", status, response.c_str());
        return ok;
    }

    bool postClimateReadingWithHttpClient(const ClimateRuntimeReading &reading, const String &payload)
    {
        (void)reading;

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

        const int status = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(payload.c_str())), payload.length());
        const String response = http.getString();
        http.end();
        client.stop();
        delay(CLIMATE_POST_UPLOAD_SETTLE_MS);

        Serial.printf("[CLIMA][UPLOAD] HTTP %d resposta=%s\n", status, response.substring(0, 160).c_str());
        return status >= 200 && status < 300;
    }

    bool postClimateReading(const ClimateRuntimeReading &reading)
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
        Serial.printf("[CLIMA][UPLOAD] Enviando leitura: ldr=%d temp=%.2f umid=%.2f lamp=%s fan=%s modo=%s\n",
                      reading.ldrRaw,
                      reading.temperatureC,
                      reading.humidityPercent,
                      reading.lampOn ? "ON" : "OFF",
                      reading.fanOn ? "ON" : "OFF",
                      CLIMATE_UPLOAD_USE_HTTPCLIENT ? "HTTPClient" : "TLS_RAW");

        if (CLIMATE_UPLOAD_USE_HTTPCLIENT)
        {
            return postClimateReadingWithHttpClient(reading, payload);
        }
        return postClimateReadingWithRawTls(reading, payload);
    }

    ClimateRuntimeReading readClimateSensors(bool evaluateFan = false, bool forceFanCheck = false)
    {
        ClimateRuntimeReading reading = lastReading;
        reading.valid = true;
        reading.ldrRaw = readLdrRaw();
        reading.ldrPercent = ldrPercentFromRaw(reading.ldrRaw);
        reading.lowLight = reading.ldrRaw <= LDR_DARK_THRESHOLD_RAW;
        reading.autoFanTriggered = false;
        reading.fanEvent = false;

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
        if (evaluateFan)
        {
            applyFanAutomation(&reading, forceFanCheck);
        }
        else
        {
            fillFanMetadata(&reading);
        }
        return reading;
    }

    void publishFanTimeoutIfNeeded()
    {
        if (!isFanOn() || fanAutoOffAtMs == 0 || millis() < fanAutoOffAtMs)
            return;

        Serial.println("[CLIMA][VENTOINHA] Timeout de seguranca atingido. Desligando ventoinha.");
        setFan(false);
        fanAutoOffAtMs = 0;

        ClimateRuntimeReading timeoutReading = readClimateSensors(false);
        timeoutReading.fanOn = isFanOn();
        timeoutReading.fanReason = "timeout_off";
        timeoutReading.autoFanTriggered = false;
        timeoutReading.fanEvent = true;
        fillFanMetadata(&timeoutReading);
        lastReading = timeoutReading;
        postClimateReading(lastReading);
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

    void updateFanConfigFromJson(JsonObject command)
    {
        const float threshold = jsonFloatOr(command, "fan_temp_threshold_c", "temperatura_limite_c", fanTempThresholdC);
        const unsigned long intervalMs = jsonULongOr(command, "fan_check_interval_ms", "intervalo_verificacao_ms", fanCheckIntervalMs);
        const unsigned long timeoutMs = jsonULongOr(command, "fan_timeout_ms", "timeout_ventoinha_ms", fanTimeoutMs);

        if (threshold >= 10.0f && threshold <= 60.0f)
            fanTempThresholdC = threshold;
        fanCheckIntervalMs = intervalMs < MIN_FAN_CHECK_INTERVAL_MS ? MIN_FAN_CHECK_INTERVAL_MS : intervalMs;
        fanTimeoutMs = timeoutMs < MIN_FAN_TIMEOUT_MS ? MIN_FAN_TIMEOUT_MS : timeoutMs;
        if (fanTimeoutMs > MAX_FAN_TIMEOUT_MS)
            fanTimeoutMs = MAX_FAN_TIMEOUT_MS;

        Serial.printf("[CLIMA][CFG] Ventoinha threshold=%.2fC check_ms=%lu timeout_ms=%lu\n",
                      fanTempThresholdC,
                      fanCheckIntervalMs,
                      fanTimeoutMs);

        lastReading.fanReason = "config_updated";
        fillFanMetadata(&lastReading);
    }
}

void setupClimateManager()
{
    Wire.begin(HDC1080_SDA_PIN, HDC1080_SCL_PIN, HDC1080_I2C_FREQUENCY_HZ);
    Wire.setTimeOut(100);

    float temperatureC = NAN;
    float humidityPercent = NAN;
    hdcOnline = readHdc1080(&temperatureC, &humidityPercent);

    Serial.printf("[CLIMA][CFG] ldr_gpio=%d adc=12bits threshold=%d hysteresis=%d lamp_gpio=%d fan_gpio=%d fan_threshold=%.2fC fan_check_ms=%lu fan_timeout_ms=%lu hdc1080=%s sda=%d scl=%d addr=0x%02x interval_ms=%lu\n",
                  PIN_LDR_ADC,
                  LDR_DARK_THRESHOLD_RAW,
                  LDR_LIGHT_HYSTERESIS_RAW,
                  PIN_RELE_LAMPADA,
                  PIN_VENTOINHA,
                  fanTempThresholdC,
                  fanCheckIntervalMs,
                  fanTimeoutMs,
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
    fillFanMetadata(&lastReading);
}

void processClimateAutomation()
{
    publishFanTimeoutIfNeeded();

    if (millis() - lastClimateReadMs < CLIMATE_INTERVAL_MS)
        return;
    lastClimateReadMs = millis();

    const bool shouldCheckFan = millis() - lastFanCheckMs >= fanCheckIntervalMs;
    lastReading = readClimateSensors(shouldCheckFan, false);
    Serial.printf("[CLIMA] ldr=%d(%.1f%%) temp=%.2fC umid=%.2f%% hdc=%s lamp=%s motivo=%s fan=%s fan_motivo=%s\n",
                  lastReading.ldrRaw,
                  lastReading.ldrPercent,
                  lastReading.temperatureC,
                  lastReading.humidityPercent,
                  lastReading.hdcAvailable ? "OK" : "FALHA",
                  lastReading.lampOn ? "ON" : "OFF",
                  lastReading.lampReason,
                  lastReading.fanOn ? "ON" : "OFF",
                  lastReading.fanReason);

    postClimateReading(lastReading);
}

bool handleClimateCommand(JsonObject command)
{
    const char *action = command["comando"] | "";
    const bool status = command["status"] | true;

    if (strcmp(action, "configurar_clima") == 0 || strcmp(action, "clima_config") == 0)
    {
        updateFanConfigFromJson(command);
        lastReading.fanEvent = true;
        postClimateReading(lastReading);
        return true;
    }

    if (strcmp(action, "ventilar") == 0 || strcmp(action, "ventoinha") == 0 || strcmp(action, "fan") == 0)
    {
        setFan(status);
        fanAutoOffAtMs = status ? millis() + fanTimeoutMs : 0;

        ClimateRuntimeReading commandReading = readClimateSensors(false);
        commandReading.fanOn = isFanOn();
        commandReading.autoFanTriggered = false;
        commandReading.fanEvent = true;
        commandReading.fanReason = status ? "manual_on" : "manual_off";
        fillFanMetadata(&commandReading);
        lastReading = commandReading;

        Serial.printf("[CLIMA][CMD] Ventoinha manual=%s timeout_ms=%lu\n",
                      status ? "ON" : "OFF",
                      fanTimeoutMs);
        postClimateReading(lastReading);
        return true;
    }

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

    ClimateRuntimeReading commandReading = readClimateSensors(false);
    commandReading.lampOn = isLampOn();
    commandReading.autoLightTriggered = false;
    commandReading.lampReason = status ? "manual_on" : "manual_off";
    fillFanMetadata(&commandReading);
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
    if (lastReading.hdcAvailable)
    {
        clima["temp_c"] = lastReading.temperatureC;
        clima["umidade_percentual"] = lastReading.humidityPercent;
    }
    clima["hdc1080_disponivel"] = lastReading.hdcAvailable;

    JsonObject iluminacao = doc["iluminacao"].to<JsonObject>();
    iluminacao["lampada_on"] = isLampOn();
    iluminacao["motivo"] = lastReading.lampReason;
    iluminacao["override_manual"] = isManualOverrideActive();

    JsonObject ventilacao = doc["ventilacao"].to<JsonObject>();
    ventilacao["ventoinha_on"] = isFanOn();
    ventilacao["motivo"] = lastReading.fanReason;
    ventilacao["limite_temp_c"] = fanTempThresholdC;
    ventilacao["intervalo_verificacao_ms"] = fanCheckIntervalMs;
    ventilacao["timeout_ms"] = fanTimeoutMs;
}

ClimateReading getLastClimateReading()
{
    ClimateReading reading = {};
    reading.valid = lastReading.valid;
    reading.ldrRaw = lastReading.ldrRaw;
    reading.ldrPercent = lastReading.ldrPercent;
    reading.hdcAvailable = lastReading.hdcAvailable;
    reading.temperatureC = lastReading.temperatureC;
    reading.humidityPercent = lastReading.humidityPercent;
    reading.lampOn = lastReading.lampOn;
    reading.lowLight = lastReading.lowLight;
    reading.autoLightTriggered = lastReading.autoLightTriggered;
    reading.lampReason = lastReading.lampReason;
    return reading;
}
