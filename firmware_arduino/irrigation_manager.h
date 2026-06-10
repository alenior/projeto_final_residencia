#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void setupIrrigationManager();
void processIrrigationAutomation();
bool handleIrrigationCommand(JsonObject command);
void appendIrrigationTelemetry(JsonDocument &doc);
