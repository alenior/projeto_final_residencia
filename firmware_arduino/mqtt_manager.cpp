#include "mqtt_manager.h"
#include "config.h"
#include "wifi_manager.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <cstring>

namespace
{
    WiFiClient wifiClient;
    PubSubClient mqtt(wifiClient);
    MqttCommandCallback commandCallback = nullptr;
    unsigned long lastReconnectAttemptMs = 0;

    String topicBase()
    {
        return String("estufa/") + MQTT_NAMESPACE + "/" + DEVICE_ID;
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

        StaticJsonDocument<1024> doc;
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
        if (mqtt.connected())
            return true;

        mqtt.setServer(MQTT_BROKER, MQTT_PORT);
        mqtt.setCallback(onMqttMessage);
        mqtt.setKeepAlive(MQTT_KEEPALIVE_SECONDS);

        Serial.printf("[MQTT] Conectando em %s:%d...\n", MQTT_BROKER, MQTT_PORT);
        bool connected;
        if (strlen(MQTT_USER) > 0)
        {
            connected = mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, topicStatus().c_str(), 0, true, "{\"online\":false}");
        }
        else
        {
            connected = mqtt.connect(DEVICE_ID, topicStatus().c_str(), 0, true, "{\"online\":false}");
        }

        if (!connected)
        {
            Serial.printf("[MQTT][WARN] Falha na conexao, state=%d\n", mqtt.state());
            return false;
        }

        mqtt.subscribe(mqttCommandTopic().c_str());
        mqtt.subscribe(topicCommandEnglish().c_str());
        mqtt.subscribe(topicLegacyCommand().c_str());
        mqtt.subscribe(topicLegacyCommandEnglish().c_str());
        mqtt.subscribe(topicGeneralCommand().c_str());

        publishStatus(true);
        mqtt.publish(topicTest().c_str(), "{\"evento\":\"boot\",\"firmware\":\"arduino\"}");

        Serial.printf("[MQTT] pub: %s | %s | %s\n", topicTelemetry().c_str(), topicStatus().c_str(), topicCamera().c_str());
        Serial.printf("[MQTT] sub: %s | %s | %s | %s | %s\n",
                      mqttCommandTopic().c_str(),
                      topicCommandEnglish().c_str(),
                      topicLegacyCommand().c_str(),
                      topicLegacyCommandEnglish().c_str(),
                      topicGeneralCommand().c_str());
        return true;
    }
}

void setupMqtt(MqttCommandCallback callback)
{
    commandCallback = callback;
    connectMqtt();
}

void mqttLoop()
{
    if (!mqtt.connected())
    {
        if (millis() - lastReconnectAttemptMs > 5000UL)
        {
            lastReconnectAttemptMs = millis();
            connectMqtt();
        }
        return;
    }
    mqtt.loop();
}

bool publishTelemetry(const String &payloadJson)
{
    if (!connectMqtt())
        return false;
    return mqtt.publish(topicTelemetry().c_str(), payloadJson.c_str());
}

bool publishStatus(bool online)
{
    if (!connectMqtt())
        return false;
    String payload = String("{\"online\":") + (online ? "true" : "false") + ",\"ip\":\"" + localIpString() + "\"}";
    return mqtt.publish(topicStatus().c_str(), payload.c_str(), true);
}

bool publishCameraEvent(const String &payloadJson)
{
    if (!connectMqtt())
        return false;
    return mqtt.publish(topicCamera().c_str(), payloadJson.c_str());
}

String mqttCommandTopic()
{
    return topicBase() + "/comandos";
}
