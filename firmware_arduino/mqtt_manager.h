#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*MqttCommandCallback)(JsonObject command);

void setupMqtt(MqttCommandCallback callback);
void mqttLoop();
bool publishTelemetry(const String &payloadJson);
bool publishStatus(bool online);
bool publishCameraEvent(const String &payloadJson);
String mqttCommandTopic();
