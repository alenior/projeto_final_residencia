#include <Arduino.h>
#include <cstring>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "actuators.h"
#include "camera_manager.h"
#include "config.h"
#include "mqtt_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

unsigned long lastTelemetryMs = 0;

String buildTelemetryJson()
{
    StaticJsonDocument<512> doc;
    doc["timestamp"] = nowIso8601();
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["psram_free"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    doc["wifi_ip"] = localIpString();
    doc["movimento"] = readMotion();
    doc["solo_raw"] = readSoilRaw();
    doc["ldr_raw"] = readLdrRaw();

    String output;
    serializeJson(doc, output);
    return output;
}

void handleCommand(JsonObject command)
{
    const char *action = command["comando"] | "";
    const bool status = command["status"] | true;

    if (strcmp(action, "irrigar") == 0)
    {
        setPump(status);
    }
    else if (strcmp(action, "aquecer") == 0)
    {
        setLamp(status);
    }
    else if (strcmp(action, "ventilar") == 0)
    {
        setFan(status);
    }
    else if (strcmp(action, "capturar") == 0 && status)
    {
        const bool ok = captureAndUpload("manual");
        publishCameraEvent(String("{\"evento\":\"captura_manual\",\"ok\":") + (ok ? "true" : "false") + "}");
    }
    else if (strcmp(action, "configurar_camera") == 0 || strcmp(action, "camera_config") == 0)
    {
        updateCameraScheduleFromJson(command);
    }
    else
    {
        Serial.printf("[CMD][WARN] Comando nao tratado: %s\n", action);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==================== BOOT ARDUINO ====================");
    Serial.printf("EstufaIoT Arduino firmware device=%s namespace=%s\n", DEVICE_ID, MQTT_NAMESPACE);
    Serial.printf("GPIOs -> bomba:%d lampada:%d pir:%d solo:%d ldr:%d ventoinha:%d\n",
                  PIN_RELE_BOMBA, PIN_RELE_LAMPADA, PIN_PIR, PIN_SOLO_ADC, PIN_LDR_ADC, PIN_VENTOINHA);
    Serial.printf("Heap=%u PSRAM livre=%u\n", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    Serial.println("======================================================");

    setupActuators();
    setupWiFi();
    setupTimeSync();
    setupCameraManager();
    setupMqtt(handleCommand);
}

void loop()
{
    ensureWiFiConnected();
    mqttLoop();

    if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)
    {
        lastTelemetryMs = millis();
        publishTelemetry(buildTelemetryJson());
    }

    if (isAutoCaptureDue())
    {
        const bool ok = captureAndUpload("automatico");
        publishCameraEvent(String("{\"evento\":\"captura_automatica\",\"ok\":") + (ok ? "true" : "false") + "}");
    }

    delay(10);
}
