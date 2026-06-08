#include <Arduino.h>
#include <cstring>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "actuators.h"
#include "camera_manager.h"
#include "climate_manager.h"
#include "config.h"
#include "mqtt_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

// Declaracao explicita para evitar falha de resolucao no preprocessamento da IDE Arduino.
extern void printCameraUploadDiagnostic();

#ifndef PIN_BOTAO_CAMERA
#define PIN_BOTAO_CAMERA 45
#endif
#ifndef CAMERA_BUTTON_ENABLED
#define CAMERA_BUTTON_ENABLED true
#endif
#ifndef CAMERA_BUTTON_ACTIVE_LOW
#define CAMERA_BUTTON_ACTIVE_LOW true
#endif
#ifndef CAMERA_BUTTON_DEBOUNCE_MS
#define CAMERA_BUTTON_DEBOUNCE_MS 80UL
#endif
#ifndef CAMERA_BUTTON_COOLDOWN_MS
#define CAMERA_BUTTON_COOLDOWN_MS 5000UL
#endif

unsigned long lastTelemetryMs = 0;
unsigned long lastMqttDebugMs = 0;
unsigned long lastCameraButtonChangeMs = 0;
unsigned long lastCameraButtonCaptureMs = 0;
bool lastCameraButtonReading = !CAMERA_BUTTON_ACTIVE_LOW;
bool stableCameraButtonState = !CAMERA_BUTTON_ACTIVE_LOW;

const char *resetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "UNKNOWN";
    }
}

void setupLocalCaptureButton()
{
    if (!CAMERA_BUTTON_ENABLED)
        return;
    pinMode(PIN_BOTAO_CAMERA, CAMERA_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    const bool reading = digitalRead(PIN_BOTAO_CAMERA);
    lastCameraButtonReading = reading;
    stableCameraButtonState = reading;
    Serial.printf("[BOTAO_CAMERA] habilitado gpio=%d modo=%s\n",
                  PIN_BOTAO_CAMERA,
                  CAMERA_BUTTON_ACTIVE_LOW ? "INPUT_PULLUP/ativo_em_GND" : "INPUT_PULLDOWN/ativo_em_3V3");
}

void publishCaptureResult(const char *eventName, bool ok, const char *origin)
{
    String payload = String("{\"evento\":\"") + eventName +
                     "\",\"ok\":" + (ok ? "true" : "false") +
                     ",\"origem\":\"" + origin + "\"}";
    publishCameraEvent(payload);
}

void handleLocalCaptureButton()
{
    if (!CAMERA_BUTTON_ENABLED)
        return;

    const unsigned long now = millis();
    const bool reading = digitalRead(PIN_BOTAO_CAMERA);
    if (reading != lastCameraButtonReading)
    {
        lastCameraButtonReading = reading;
        lastCameraButtonChangeMs = now;
    }

    if (now - lastCameraButtonChangeMs < CAMERA_BUTTON_DEBOUNCE_MS)
        return;
    if (reading == stableCameraButtonState)
        return;

    stableCameraButtonState = reading;
    const bool pressed = CAMERA_BUTTON_ACTIVE_LOW ? (stableCameraButtonState == LOW) : (stableCameraButtonState == HIGH);
    if (!pressed)
        return;

    if (now - lastCameraButtonCaptureMs < CAMERA_BUTTON_COOLDOWN_MS)
    {
        Serial.println("[BOTAO_CAMERA][WARN] Captura ignorada por cooldown.");
        return;
    }

    lastCameraButtonCaptureMs = now;
    Serial.println("[BOTAO_CAMERA] Captura manual local solicitada.");
    const bool ok = captureAndUpload("manual_button");
    publishCaptureResult("captura_botao", ok, "botao_gpio45");
}

String buildTelemetryJson()
{
    JsonDocument doc;
    doc["timestamp"] = nowIso8601();
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["psram_free"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    doc["wifi_ip"] = localIpString();
    doc["movimento"] = readMotion();
    doc["solo_raw"] = readSoilRaw();
    appendClimateTelemetry(doc);

    String output;
    serializeJson(doc, output);
    return output;
}

void handleCommand(JsonObject command)
{
    const char *action = command["comando"] | "";
    const bool status = command["status"] | true;

    Serial.printf("[CMD] comando=%s status=%s\n", action, status ? "true" : "false");

    if (handleClimateCommand(command)) {
        return;
    }

    if (strcmp(action, "irrigar") == 0)
    {
        setPump(status);
    }
    else if (strcmp(action, "ventilar") == 0)
    {
        setFan(status);
    }
    else if (strcmp(action, "capturar") == 0 && status)
    {
        Serial.println("[CMD] Acionando captura manual da camera.");
        const char *reason = command["reason"] | "manual";
        const bool ok = captureAndUpload(reason);
        publishCaptureResult("captura_manual", ok, "mqtt");
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
    const esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.printf("Reset reason=%d(%s)\n", static_cast<int>(resetReason), resetReasonName(resetReason));
    printCameraUploadDiagnostic();
    Serial.printf("GPIOs -> bomba:%d lampada_led:%d pir:%d solo:%d ldr:%d ventoinha:%d botao_camera:%d\n",
                  PIN_RELE_BOMBA, PIN_RELE_LAMPADA, PIN_PIR, PIN_SOLO_ADC, PIN_LDR_ADC, PIN_VENTOINHA, PIN_BOTAO_CAMERA);
    Serial.printf("Heap=%lu PSRAM livre=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    Serial.println("======================================================");

    setupActuators();
    setupLocalCaptureButton();
    setupWiFi();
    setupTimeSync();
    setupClimateManager();
    setupCameraManager();
    setupMqtt(handleCommand);
}

void loop()
{
    ensureWiFiConnected();
    mqttLoop();
    handleLocalCaptureButton();
    processClimateAutomation();

    if (millis() - lastMqttDebugMs >= 30000UL)
    {
        lastMqttDebugMs = millis();
        Serial.printf("[MQTT][DBG] connected=%s state=%d uptime_ms=%lu\n",
                      isMqttConnected() ? "true" : "false",
                      mqttConnectionState(),
                      millis());
    }

    if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)
    {
        lastTelemetryMs = millis();
        publishTelemetry(buildTelemetryJson());
    }

    if (isAutoCaptureDue())
    {
        const bool ok = captureAndUpload("automatico");
        publishCaptureResult("captura_automatica", ok, "agenda");
    }

    delay(10);
}
