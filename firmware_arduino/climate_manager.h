#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct ClimateReading
{
    bool valid;
    int ldrRaw;
    float ldrPercent;
    bool hdcAvailable;
    float temperatureC;
    float humidityPercent;
    bool lampOn;
    bool lowLight;
    bool autoLightTriggered;
    const char *lampReason;
};

void setupClimateManager();
void processClimateAutomation();
bool handleClimateCommand(JsonObject command);
void appendClimateTelemetry(JsonDocument &doc);
ClimateReading getLastClimateReading();
