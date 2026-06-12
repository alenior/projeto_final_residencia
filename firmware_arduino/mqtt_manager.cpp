#include "mqtt_manager.h"
#include "config.h"
#include "wifi_manager.h"

#ifndef MQTT_USE_PUBSUBCLIENT
#define MQTT_USE_PUBSUBCLIENT 0
#endif
#ifndef MQTT_USE_RAW_CLIENT
#define MQTT_USE_RAW_CLIENT 1
#endif
#ifndef MQTT_BOOT_SAFE_MODE
#define MQTT_BOOT_SAFE_MODE 1
#endif
#ifndef MQTT_CONNECT_ON_BOOT
#define MQTT_CONNECT_ON_BOOT (!MQTT_BOOT_SAFE_MODE)
#endif
#ifndef MQTT_SOCKET_TIMEOUT_MS
#define MQTT_SOCKET_TIMEOUT_MS 3000UL
#endif
#ifndef MQTT_RECONNECT_INTERVAL_MS
#define MQTT_RECONNECT_INTERVAL_MS 10000UL
#endif

#if MQTT_USE_PUBSUBCLIENT && !MQTT_USE_RAW_CLIENT
#include <PubSubClient.h>
#define ESTUFA_HAS_PUBSUBCLIENT 1
#else
#define ESTUFA_HAS_PUBSUBCLIENT 0
#endif

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdio>

#ifndef ESTUFA_UNUSED
#define ESTUFA_UNUSED __attribute__((unused))
#endif

namespace
{
    WiFiClient mqttNetClient;
    MqttCommandCallback commandCallback = nullptr;
    ESTUFA_UNUSED unsigned long lastReconnectAttemptMs = 0;
    unsigned long lastRawPingMs = 0;
    uint16_t nextRawPacketId = 1;
    int mqttState = -99;

    bool connectMqtt();

    String topicBase()
    {
        return String("estufa/") + MQTT_NAMESPACE + "/" + DEVICE_ID;
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

    ESTUFA_UNUSED String topicTelemetry() { return topicBase() + "/telemetria"; }
    ESTUFA_UNUSED String topicStatus() { return topicBase() + "/status"; }
    ESTUFA_UNUSED String topicCamera() { return topicBase() + "/camera"; }

    void dispatchMqttJson(const char *topic, const String &raw)
    {
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

#if MQTT_USE_RAW_CLIENT
    bool rawWriteByte(uint8_t value)
    {
        return mqttNetClient.write(&value, 1) == 1;
    }

    bool rawWriteRemainingLength(size_t length)
    {
        do
        {
            uint8_t encoded = length % 128;
            length /= 128;
            if (length > 0)
                encoded |= 0x80;
            if (!rawWriteByte(encoded))
                return false;
        } while (length > 0);
        return true;
    }

    bool rawWriteMqttString(const char *value)
    {
        const size_t length = strlen(value);
        if (length > 65535UL)
            return false;
        const uint8_t header[] = {
            static_cast<uint8_t>((length >> 8) & 0xFF),
            static_cast<uint8_t>(length & 0xFF),
        };
        return mqttNetClient.write(header, sizeof(header)) == sizeof(header) &&
               mqttNetClient.write(reinterpret_cast<const uint8_t *>(value), length) == length;
    }

    bool rawReadByte(uint8_t *value, unsigned long timeoutMs = MQTT_SOCKET_TIMEOUT_MS)
    {
        const unsigned long deadline = millis() + timeoutMs;
        while (!mqttNetClient.available() && mqttNetClient.connected() && static_cast<long>(deadline - millis()) > 0)
        {
            yield();
            delay(1);
        }
        if (!mqttNetClient.available())
            return false;
        const int readValue = mqttNetClient.read();
        if (readValue < 0)
            return false;
        *value = static_cast<uint8_t>(readValue);
        return true;
    }

    bool rawReadRemainingLength(size_t *length)
    {
        *length = 0;
        size_t multiplier = 1;
        for (uint8_t i = 0; i < 4; i++)
        {
            uint8_t encoded = 0;
            if (!rawReadByte(&encoded))
                return false;
            *length += (encoded & 127) * multiplier;
            if ((encoded & 128) == 0)
                return true;
            multiplier *= 128;
        }
        return false;
    }

    void rawDiscard(size_t length)
    {
        while (length > 0 && mqttNetClient.connected())
        {
            if (mqttNetClient.available())
            {
                mqttNetClient.read();
                length--;
            }
            else
            {
                yield();
                delay(1);
            }
        }
    }

    bool rawReadPacketHeader(uint8_t *packetType, size_t *remainingLength)
    {
        uint8_t fixedHeader = 0;
        if (!rawReadByte(&fixedHeader))
            return false;
        if (!rawReadRemainingLength(remainingLength))
            return false;
        *packetType = fixedHeader >> 4;
        return true;
    }

    bool rawWaitForPacket(uint8_t expectedType, size_t *remainingLength)
    {
        uint8_t packetType = 0;
        if (!rawReadPacketHeader(&packetType, remainingLength))
            return false;
        if (packetType != expectedType)
        {
            rawDiscard(*remainingLength);
            Serial.printf("[MQTT][RAW][WARN] Pacote inesperado tipo=%u esperado=%u\n", packetType, expectedType);
            return false;
        }
        return true;
    }

    void rawDisconnect()
    {
        if (mqttNetClient.connected())
        {
            const uint8_t disconnectPacket[] = {0xE0, 0x00};
            mqttNetClient.write(disconnectPacket, sizeof(disconnectPacket));
        }
        mqttNetClient.stop();
        mqttState = -1;
    }

    bool rawSubscribeOne(const char *topic)
    {
        const uint16_t packetId = nextRawPacketId++;
        const size_t remainingLength = 2 + 2 + strlen(topic) + 1;
        if (!rawWriteByte(0x82) || !rawWriteRemainingLength(remainingLength))
            return false;
        if (!rawWriteByte(static_cast<uint8_t>((packetId >> 8) & 0xFF)) || !rawWriteByte(static_cast<uint8_t>(packetId & 0xFF)))
            return false;
        if (!rawWriteMqttString(topic) || !rawWriteByte(0x00))
            return false;
        mqttNetClient.flush();

        size_t ackLength = 0;
        if (!rawWaitForPacket(9, &ackLength))
            return false;
        rawDiscard(ackLength);
        Serial.printf("[MQTT][SUB][RAW] %s => OK\n", topic);
        return true;
    }

    bool rawPublish(const char *topic, const char *payload, bool retained = false)
    {
        if (!isWiFiConnected())
            return false;
        if (!mqttNetClient.connected() && !connectMqtt())
            return false;

        const size_t topicLength = strlen(topic);
        const size_t payloadLength = strlen(payload);
        const size_t remainingLength = 2 + topicLength + payloadLength;
        const uint8_t fixedHeader = retained ? 0x31 : 0x30;
        if (!rawWriteByte(fixedHeader) || !rawWriteRemainingLength(remainingLength))
            return false;
        if (!rawWriteMqttString(topic))
            return false;
        if (payloadLength > 0 && mqttNetClient.write(reinterpret_cast<const uint8_t *>(payload), payloadLength) != payloadLength)
            return false;
        mqttNetClient.flush();
        return true;
    }

    bool rawConnectMqtt()
    {
        if (!isWiFiConnected())
            return false;
        if (mqttNetClient.connected())
            return true;

        mqttNetClient.stop();
        mqttState = -2;
        Serial.printf("[MQTT][RAW] Conectando em %s:%d heap=%lu...\n", MQTT_BROKER, MQTT_PORT, static_cast<unsigned long>(ESP.getFreeHeap()));
        yield();
        delay(10);

        if (!mqttNetClient.connect(MQTT_BROKER, MQTT_PORT, MQTT_SOCKET_TIMEOUT_MS))
        {
            Serial.println("[MQTT][RAW][WARN] Falha no TCP connect.");
            mqttState = -3;
            mqttNetClient.stop();
            return false;
        }
        mqttNetClient.setTimeout(1);

        const bool hasUser = strlen(MQTT_USER) > 0;
        const bool hasPassword = strlen(MQTT_PASSWORD) > 0;
        uint8_t connectFlags = 0x02; // clean session
        if (hasUser)
            connectFlags |= 0x80;
        if (hasPassword)
            connectFlags |= 0x40;

        size_t remainingLength = 10 + 2 + strlen(DEVICE_ID);
        if (hasUser)
            remainingLength += 2 + strlen(MQTT_USER);
        if (hasPassword)
            remainingLength += 2 + strlen(MQTT_PASSWORD);

        if (!rawWriteByte(0x10) || !rawWriteRemainingLength(remainingLength) ||
            !rawWriteMqttString("MQTT") || !rawWriteByte(0x04) || !rawWriteByte(connectFlags) ||
            !rawWriteByte(static_cast<uint8_t>((MQTT_KEEPALIVE_SECONDS >> 8) & 0xFF)) ||
            !rawWriteByte(static_cast<uint8_t>(MQTT_KEEPALIVE_SECONDS & 0xFF)) ||
            !rawWriteMqttString(DEVICE_ID) ||
            (hasUser && !rawWriteMqttString(MQTT_USER)) ||
            (hasPassword && !rawWriteMqttString(MQTT_PASSWORD)))
        {
            Serial.println("[MQTT][RAW][ERRO] Falha escrevendo pacote CONNECT.");
            rawDisconnect();
            return false;
        }
        mqttNetClient.flush();

        size_t connackLength = 0;
        if (!rawWaitForPacket(2, &connackLength) || connackLength < 2)
        {
            Serial.println("[MQTT][RAW][ERRO] CONNACK ausente/invalido.");
            rawDisconnect();
            return false;
        }
        uint8_t ackFlags = 0;
        uint8_t returnCode = 0xFF;
        rawReadByte(&ackFlags);
        rawReadByte(&returnCode);
        if (connackLength > 2)
            rawDiscard(connackLength - 2);
        if (returnCode != 0)
        {
            Serial.printf("[MQTT][RAW][WARN] Broker recusou conexao, rc=%u\n", returnCode);
            mqttState = returnCode;
            rawDisconnect();
            return false;
        }

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

        rawSubscribeOne(commandTopic);
        rawSubscribeOne(commandEnglishTopic);
        rawSubscribeOne(legacyCommandTopic);
        rawSubscribeOne(legacyCommandEnglishTopic);
        rawSubscribeOne(generalCommandTopic);

        char onlinePayload[192];
        snprintf(onlinePayload, sizeof(onlinePayload),
                 "{\"online\":true,\"device_id\":\"%s\",\"namespace\":\"%s\",\"firmware\":\"arduino\",\"ip\":\"%s\",\"uptime_ms\":%lu}",
                 DEVICE_ID,
                 MQTT_NAMESPACE,
                 localIpString().c_str(),
                 millis());
        onlinePayload[sizeof(onlinePayload) - 1] = '\0';
        rawPublish(statusTopic, onlinePayload, true);
        rawPublish(testTopic, "{\"evento\":\"boot\",\"firmware\":\"arduino\"}");

        mqttState = 0;
        lastRawPingMs = millis();
        Serial.printf("[MQTT][RAW] pub: %s | %s | %s\n", telemetryTopic, statusTopic, cameraTopic);
        Serial.printf("[MQTT][RAW] sub: %s | %s | %s | %s | %s\n",
                      commandTopic,
                      commandEnglishTopic,
                      legacyCommandTopic,
                      legacyCommandEnglishTopic,
                      generalCommandTopic);
        return true;
    }

    bool processRawPublish(size_t remainingLength)
    {
        if (remainingLength < 2)
        {
            rawDiscard(remainingLength);
            return false;
        }

        uint8_t msb = 0;
        uint8_t lsb = 0;
        if (!rawReadByte(&msb) || !rawReadByte(&lsb))
            return false;
        const size_t topicLength = (static_cast<size_t>(msb) << 8) | lsb;
        if (topicLength + 2 > remainingLength || topicLength >= 96)
        {
            rawDiscard(remainingLength > 2 ? remainingLength - 2 : 0);
            return false;
        }

        char topic[96];
        for (size_t i = 0; i < topicLength; i++)
        {
            uint8_t c = 0;
            if (!rawReadByte(&c))
                return false;
            topic[i] = static_cast<char>(c);
        }
        topic[topicLength] = '\0';

        const size_t payloadLength = remainingLength - 2 - topicLength;
        String payload;
        payload.reserve(payloadLength + 1);
        for (size_t i = 0; i < payloadLength; i++)
        {
            uint8_t c = 0;
            if (!rawReadByte(&c))
                return false;
            payload += static_cast<char>(c);
        }
        dispatchMqttJson(topic, payload);
        return true;
    }

    ESTUFA_UNUSED void rawMqttLoop()
    {
        if (!mqttNetClient.connected())
            return;

        while (mqttNetClient.available())
        {
            uint8_t packetType = 0;
            size_t remainingLength = 0;
            if (!rawReadPacketHeader(&packetType, &remainingLength))
            {
                rawDisconnect();
                return;
            }

            if (packetType == 3)
            {
                processRawPublish(remainingLength);
            }
            else if (packetType == 13)
            {
                rawDiscard(remainingLength);
            }
            else
            {
                rawDiscard(remainingLength);
            }
            yield();
        }

        const unsigned long pingIntervalMs = (MQTT_KEEPALIVE_SECONDS > 10 ? (MQTT_KEEPALIVE_SECONDS - 5) : MQTT_KEEPALIVE_SECONDS) * 1000UL;
        if (millis() - lastRawPingMs >= pingIntervalMs)
        {
            const uint8_t pingPacket[] = {0xC0, 0x00};
            mqttNetClient.write(pingPacket, sizeof(pingPacket));
            mqttNetClient.flush();
            lastRawPingMs = millis();
        }
    }
#endif

#if ESTUFA_HAS_PUBSUBCLIENT
    // Fallback legado: mantido para comparacao, mas o padrao do projeto usa MQTT_USE_RAW_CLIENT=1.
    PubSubClient pubSubClient(mqttNetClient);

    void onMqttMessage(char *topic, byte *payload, unsigned int length)
    {
        String raw;
        raw.reserve(length + 1);
        for (unsigned int i = 0; i < length; i++)
            raw += static_cast<char>(payload[i]);
        dispatchMqttJson(topic, raw);
    }

    bool pubSubConnectMqtt()
    {
        if (!isWiFiConnected())
            return false;
        if (pubSubClient.connected())
            return true;

        char statusTopic[96];
        char commandTopic[96];
        makeNamespacedTopic(statusTopic, sizeof(statusTopic), "status");
        makeNamespacedTopic(commandTopic, sizeof(commandTopic), "comandos");

        pubSubClient.setServer(MQTT_BROKER, MQTT_PORT);
        pubSubClient.setCallback(onMqttMessage);
        pubSubClient.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
        pubSubClient.setSocketTimeout(3);

        Serial.printf("[MQTT][PUBSUB] Conectando em %s:%d heap=%lu...\n", MQTT_BROKER, MQTT_PORT, static_cast<unsigned long>(ESP.getFreeHeap()));
        const bool connected = strlen(MQTT_USER) > 0
                                   ? pubSubClient.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, statusTopic, 0, true, "{\"online\":false}")
                                   : pubSubClient.connect(DEVICE_ID, statusTopic, 0, true, "{\"online\":false}");
        if (!connected)
        {
            mqttState = pubSubClient.state();
            Serial.printf("[MQTT][PUBSUB][WARN] Falha na conexao, state=%d\n", mqttState);
            return false;
        }

        pubSubClient.subscribe(commandTopic);
        mqttState = 0;
        return true;
    }
#endif

    bool connectMqtt()
    {
#if MQTT_USE_RAW_CLIENT
        return rawConnectMqtt();
#elif ESTUFA_HAS_PUBSUBCLIENT
        return pubSubConnectMqtt();
#else
        return false;
#endif
    }
} // namespace

void setupMqtt(MqttCommandCallback callback)
{
    commandCallback = callback;
#if MQTT_BOOT_SAFE_MODE || !MQTT_CONNECT_ON_BOOT
    Serial.printf("[MQTT][WARN] Conexao MQTT no boot ignorada: safe_mode=%d connect_on_boot=%d raw=%d pubsub=%d.\n",
                  MQTT_BOOT_SAFE_MODE,
                  MQTT_CONNECT_ON_BOOT,
                  MQTT_USE_RAW_CLIENT,
                  ESTUFA_HAS_PUBSUBCLIENT);
#else
    connectMqtt();
#endif
}

void mqttLoop()
{
#if MQTT_BOOT_SAFE_MODE
    return;
#else
    if (!isWiFiConnected())
        return;
    if (!isMqttConnected())
    {
        if (millis() - lastReconnectAttemptMs > MQTT_RECONNECT_INTERVAL_MS)
        {
            lastReconnectAttemptMs = millis();
            connectMqtt();
        }
        return;
    }
#if MQTT_USE_RAW_CLIENT
    rawMqttLoop();
#elif ESTUFA_HAS_PUBSUBCLIENT
    pubSubClient.loop();
#endif
#endif
}

bool publishTelemetry(const String &payloadJson)
{
#if MQTT_BOOT_SAFE_MODE
    (void)payloadJson;
    return false;
#elif MQTT_USE_RAW_CLIENT
    const String topic = topicTelemetry();
    return rawPublish(topic.c_str(), payloadJson.c_str());
#elif ESTUFA_HAS_PUBSUBCLIENT
    if (!connectMqtt())
        return false;
    return pubSubClient.publish(topicTelemetry().c_str(), payloadJson.c_str());
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
#else
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
#if MQTT_USE_RAW_CLIENT
    const String topic = topicStatus();
    return rawPublish(topic.c_str(), payload.c_str(), true);
#elif ESTUFA_HAS_PUBSUBCLIENT
    if (!connectMqtt())
        return false;
    return pubSubClient.publish(topicStatus().c_str(), payload.c_str(), true);
#else
    return false;
#endif
#endif
}

bool publishCameraEvent(const String &payloadJson)
{
#if MQTT_BOOT_SAFE_MODE
    (void)payloadJson;
    return false;
#elif MQTT_USE_RAW_CLIENT
    const String topic = topicCamera();
    return rawPublish(topic.c_str(), payloadJson.c_str());
#elif ESTUFA_HAS_PUBSUBCLIENT
    if (!connectMqtt())
        return false;
    return pubSubClient.publish(topicCamera().c_str(), payloadJson.c_str());
#else
    (void)payloadJson;
    return false;
#endif
}

bool isMqttConnected()
{
#if MQTT_BOOT_SAFE_MODE
    return false;
#elif MQTT_USE_RAW_CLIENT
    return mqttNetClient.connected();
#elif ESTUFA_HAS_PUBSUBCLIENT
    return pubSubClient.connected();
#else
    return false;
#endif
}

int mqttConnectionState()
{
#if MQTT_BOOT_SAFE_MODE
    return -98;
#else
    return mqttState;
#endif
}

String mqttCommandTopic()
{
    return topicBase() + "/comandos";
}
