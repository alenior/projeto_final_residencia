#include "mqtt_manager.h"
#include "config.h"
#include "wifi_manager.h"

#ifndef MQTT_USE_PUBSUBCLIENT
#define MQTT_USE_PUBSUBCLIENT 0
#endif
#ifndef MQTT_BOOT_SAFE_MODE
#define MQTT_BOOT_SAFE_MODE 1
#endif
#ifndef MQTT_CONNECT_ON_BOOT
#define MQTT_CONNECT_ON_BOOT (!MQTT_BOOT_SAFE_MODE)
#endif

#if MQTT_USE_PUBSUBCLIENT
#include <PubSubClient.h>
#define ESTUFA_HAS_PUBSUBCLIENT 1
#else
#define ESTUFA_HAS_PUBSUBCLIENT 0
#endif

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdio>

namespace
{
    WiFiClient wifiClient;
    MqttCommandCallback commandCallback = nullptr;
    unsigned long lastReconnectAttemptMs = 0;

    String topicBase()
    {
        return String("estufa/") + MQTT_NAMESPACE + "/" + DEVICE_ID;
    }

#if ESTUFA_HAS_PUBSUBCLIENT
    // Instancia global: o buffer interno do PubSubClient e alocado cedo, antes de
    // camera/SD/HTTPS fragmentarem a heap. Antes era um static local criado no
    // primeiro connect(), exatamente no ponto em que algumas placas reiniciavam.
    PubSubClient mqttClient(wifiClient);

    PubSubClient &mqttClientRef()
    {
        return mqttClient;
    }

    void makeNamespacedTopic(char *output, size_t outputSize, const char *suffix)
    {
        snprintf(output, outputSize, "estufa/%s/%s/%s", MQTT_NAMESPACE, DEVICE_ID, suffix);
        output[outputSize - 1] = '\0';
    }

    void makeLegacyTopic(char *output, size_t outputSize, const char *suffix)
    {
        snprintf(output, outputSize, "estufa/%s/%s", DEVICE_ID, suffix);
        output[outputSize - 1] = '\0';
    }

    String topicTelemetry() { return topicBase() + "/telemetria"; }
    String topicStatus() { return topicBase() + "/status"; }
    String topicCamera() { return topicBase() + "/camera"; }

    void onMqttMessage(char *topic, byte *payload, unsigned int length)
    {
        String raw;
        raw.reserve(length + 1);
        for (unsigned int i = 0; i < length; i++)
            raw += static_cast<char>(payload[i]);

        Serial.printf("[MQTT][RX] %s => %s\n", topic, raw.c_str());

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, raw);
        if (error)
        {
            Serial.printf("[MQTT][WARN] JSON invalido: %s\n", error.c_str());
            return;
        }

        if (commandCallback != nullptr)
        {
            JsonObject obj = doc.as<JsonObject>();
            commandCallback(obj);
        }
    }

    bool connectMqtt()
    {
        if (!isWiFiConnected())
            return false;

        PubSubClient &client = mqttClientRef();
        if (client.connected())
            return true;

        char telemetryTopic[96];
        char statusTopic[96];
        char cameraTopic[96];
        char testTopic[96];
        char commandTopic[96];
        char commandEnglishTopic[96];
        char legacyCommandTopic[96];
        char legacyCommandEnglishTopic[96];
        const char *generalCommandTopic = "estufa/comandos";
        makeNamespacedTopic(telemetryTopic, sizeof(telemetryTopic), "telemetria");
        makeNamespacedTopic(statusTopic, sizeof(statusTopic), "status");
        makeNamespacedTopic(cameraTopic, sizeof(cameraTopic), "camera");
        makeNamespacedTopic(testTopic, sizeof(testTopic), "teste");
        makeNamespacedTopic(commandTopic, sizeof(commandTopic), "comandos");
        makeNamespacedTopic(commandEnglishTopic, sizeof(commandEnglishTopic), "commands");
        makeLegacyTopic(legacyCommandTopic, sizeof(legacyCommandTopic), "comandos");
        makeLegacyTopic(legacyCommandEnglishTopic, sizeof(legacyCommandEnglishTopic), "commands");

        client.setServer(MQTT_BROKER, MQTT_PORT);
        client.setCallback(onMqttMessage);
        client.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
        client.setSocketTimeout(3);

        Serial.printf("[MQTT] Conectando em %s:%d heap=%lu...\n", MQTT_BROKER, MQTT_PORT, static_cast<unsigned long>(ESP.getFreeHeap()));
        yield();
        delay(10);

        bool connected;
        if (strlen(MQTT_USER) > 0)
        {
            connected = client.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, statusTopic, 0, true, "{\"online\":false}");
        }
        else
        {
            connected = client.connect(DEVICE_ID, statusTopic, 0, true, "{\"online\":false}");
        }

        if (!connected)
        {
            Serial.printf("[MQTT][WARN] Falha na conexao, state=%d heap=%lu\n", client.state(), static_cast<unsigned long>(ESP.getFreeHeap()));
            client.disconnect();
            wifiClient.stop();
            return false;
        }

        Serial.printf("[MQTT][SUB] %s => %s\n", commandTopic, client.subscribe(commandTopic) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", commandEnglishTopic, client.subscribe(commandEnglishTopic) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", legacyCommandTopic, client.subscribe(legacyCommandTopic) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", legacyCommandEnglishTopic, client.subscribe(legacyCommandEnglishTopic) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", generalCommandTopic, client.subscribe(generalCommandTopic) ? "OK" : "FAIL");

        char onlinePayload[192];
        snprintf(onlinePayload, sizeof(onlinePayload),
                 "{\"online\":true,\"device_id\":\"%s\",\"namespace\":\"%s\",\"firmware\":\"arduino\",\"ip\":\"%s\",\"uptime_ms\":%lu}",
                 DEVICE_ID,
                 MQTT_NAMESPACE,
                 localIpString().c_str(),
                 millis());
        onlinePayload[sizeof(onlinePayload) - 1] = '\0';
        client.publish(statusTopic, onlinePayload, true);
        client.publish(testTopic, "{\"evento\":\"boot\",\"firmware\":\"arduino\"}");

        Serial.printf("[MQTT] pub: %s | %s | %s\n", telemetryTopic, statusTopic, cameraTopic);
        Serial.printf("[MQTT] sub: %s | %s | %s | %s | %s\n",
                      commandTopic,
                      commandEnglishTopic,
                      legacyCommandTopic,
                      legacyCommandEnglishTopic,
                      generalCommandTopic);
        return true;
    }
#endif
}

void setupMqtt(MqttCommandCallback callback)
{
    commandCallback = callback;
#if MQTT_BOOT_SAFE_MODE || !MQTT_CONNECT_ON_BOOT
    Serial.printf("[MQTT][WARN] Conexao MQTT no boot ignorada: safe_mode=%d connect_on_boot=%d pubsub=%d.\n",
                  MQTT_BOOT_SAFE_MODE,
                  MQTT_CONNECT_ON_BOOT,
                  ESTUFA_HAS_PUBSUBCLIENT);
#elif ESTUFA_HAS_PUBSUBCLIENT
    connectMqtt();
#else
    Serial.println("[MQTT][WARN] PubSubClient.h desabilitado/ausente; MQTT desabilitado nesta compilacao.");
#endif
}

void mqttLoop()
{
#if MQTT_BOOT_SAFE_MODE
    return;
#elif ESTUFA_HAS_PUBSUBCLIENT
    PubSubClient &client = mqttClientRef();
    if (!client.connected())
    {
        if (millis() - lastReconnectAttemptMs > 5000UL)
        {
            lastReconnectAttemptMs = millis();
            connectMqtt();
        }
        return;
    }
    client.loop();
#else
    (void)lastReconnectAttemptMs;
#endif
}

bool publishTelemetry(const String &payloadJson)
{
#if MQTT_BOOT_SAFE_MODE
    (void)payloadJson;
    return false;
#elif ESTUFA_HAS_PUBSUBCLIENT
    if (!connectMqtt())
        return false;
    return mqttClientRef().publish(topicTelemetry().c_str(), payloadJson.c_str());
#else
    (void)payloadJson;
    return false;
#endif
}

bool publishStatus(bool online)
{
#if MQTT_BOOT_SAFE_MODE
    (void)online;
    return false;
#elif ESTUFA_HAS_PUBSUBCLIENT
    if (!connectMqtt())
        return false;

    JsonDocument doc;
    doc["online"] = online;
    doc["device_id"] = DEVICE_ID;
    doc["namespace"] = MQTT_NAMESPACE;
    doc["firmware"] = "arduino";
    doc["ip"] = localIpString();
    doc["mac"] = WiFi.macAddress();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime_ms"] = millis();
    doc["free_heap"] = static_cast<unsigned long>(ESP.getFreeHeap());
    doc["psram_free"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    String payload;
    serializeJson(doc, payload);
    return mqttClientRef().publish(topicStatus().c_str(), payload.c_str(), true);
#else
    (void)online;
    return false;
#endif
}

bool publishCameraEvent(const String &payloadJson)
{
#if MQTT_BOOT_SAFE_MODE
    (void)payloadJson;
    return false;
#elif ESTUFA_HAS_PUBSUBCLIENT
    if (!connectMqtt())
        return false;
    return mqttClientRef().publish(topicCamera().c_str(), payloadJson.c_str());
#else
    (void)payloadJson;
    return false;
#endif
}

bool isMqttConnected()
{
#if MQTT_BOOT_SAFE_MODE
    return false;
#elif ESTUFA_HAS_PUBSUBCLIENT
    return mqttClientRef().connected();
#else
    return false;
#endif
}

int mqttConnectionState()
{
#if MQTT_BOOT_SAFE_MODE
    return -98;
#elif ESTUFA_HAS_PUBSUBCLIENT
    return mqttClientRef().state();
#else
    return -99;
#endif
}

String mqttCommandTopic()
{
    return topicBase() + "/comandos";
}
