#include "mqtt_manager.h"
#include "config.h"
#include "wifi_manager.h"

#ifndef MQTT_USE_PUBSUBCLIENT
#define MQTT_USE_PUBSUBCLIENT 0
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
    PubSubClient &mqttClientRef()
    {
        static PubSubClient client(wifiClient);
        return client;
    }

    String topicTelemetry() { return topicBase() + "/telemetria"; }
    String topicStatus() { return topicBase() + "/status"; }
    String topicCamera() { return topicBase() + "/camera"; }
    String topicTest() { return topicBase() + "/teste"; }
    String topicCommandEnglish() { return topicBase() + "/commands"; }
    String topicLegacyCommand() { return String("estufa/") + DEVICE_ID + "/comandos"; }
    String topicLegacyCommandEnglish() { return String("estufa/") + DEVICE_ID + "/commands"; }
    String topicGeneralCommand() { return "estufa/comandos"; }

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

        client.setServer(MQTT_BROKER, MQTT_PORT);
        client.setCallback(onMqttMessage);
        client.setKeepAlive(MQTT_KEEPALIVE_SECONDS);

        Serial.printf("[MQTT] Conectando em %s:%d...\n", MQTT_BROKER, MQTT_PORT);
        bool connected;
        if (strlen(MQTT_USER) > 0)
        {
            connected = client.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, topicStatus().c_str(), 0, true, "{\"online\":false}");
        }
        else
        {
            connected = client.connect(DEVICE_ID, topicStatus().c_str(), 0, true, "{\"online\":false}");
        }

        if (!connected)
        {
            Serial.printf("[MQTT][WARN] Falha na conexao, state=%d\n", client.state());
            return false;
        }

        const String commandTopic = mqttCommandTopic();
        const String commandEnglishTopic = topicCommandEnglish();
        const String legacyCommandTopic = topicLegacyCommand();
        const String legacyCommandEnglishTopic = topicLegacyCommandEnglish();
        const String generalCommandTopic = topicGeneralCommand();

        Serial.printf("[MQTT][SUB] %s => %s\n", commandTopic.c_str(), client.subscribe(commandTopic.c_str()) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", commandEnglishTopic.c_str(), client.subscribe(commandEnglishTopic.c_str()) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", legacyCommandTopic.c_str(), client.subscribe(legacyCommandTopic.c_str()) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", legacyCommandEnglishTopic.c_str(), client.subscribe(legacyCommandEnglishTopic.c_str()) ? "OK" : "FAIL");
        Serial.printf("[MQTT][SUB] %s => %s\n", generalCommandTopic.c_str(), client.subscribe(generalCommandTopic.c_str()) ? "OK" : "FAIL");

        publishStatus(true);
        client.publish(topicTest().c_str(), "{\"evento\":\"boot\",\"firmware\":\"arduino\"}");

        Serial.printf("[MQTT] pub: %s | %s | %s\n", topicTelemetry().c_str(), topicStatus().c_str(), topicCamera().c_str());
        Serial.printf("[MQTT] sub: %s | %s | %s | %s | %s\n",
                      mqttCommandTopic().c_str(),
                      topicCommandEnglish().c_str(),
                      topicLegacyCommand().c_str(),
                      topicLegacyCommandEnglish().c_str(),
                      topicGeneralCommand().c_str());
        return true;
    }
#endif
}

void setupMqtt(MqttCommandCallback callback)
{
    commandCallback = callback;
#if ESTUFA_HAS_PUBSUBCLIENT
    connectMqtt();
#else
    Serial.println("[MQTT][WARN] PubSubClient.h desabilitado/ausente; MQTT desabilitado nesta compilacao.");
#endif
}

void mqttLoop()
{
#if ESTUFA_HAS_PUBSUBCLIENT
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
#if ESTUFA_HAS_PUBSUBCLIENT
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
#if ESTUFA_HAS_PUBSUBCLIENT
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
    doc["free_heap"] = ESP.getFreeHeap();
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
#if ESTUFA_HAS_PUBSUBCLIENT
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
#if ESTUFA_HAS_PUBSUBCLIENT
    return mqttClientRef().connected();
#else
    return false;
#endif
}

int mqttConnectionState()
{
#if ESTUFA_HAS_PUBSUBCLIENT
    return mqttClientRef().state();
#else
    return -99;
#endif
}

String mqttCommandTopic()
{
    return topicBase() + "/comandos";
}
