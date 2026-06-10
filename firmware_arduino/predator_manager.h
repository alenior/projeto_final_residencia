#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void setupPredatorManager();
void processPredatorMonitoring();
bool handlePredatorCommand(JsonObject command);
void appendPredatorTelemetry(JsonDocument &doc);
