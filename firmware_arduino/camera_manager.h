#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct CameraScheduleConfig
{
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t intervalHours;
};

void setupCameraManager();
bool initCamera();
bool captureAndUpload(const char *reason);
bool isAutoCaptureDue();
void updateCameraScheduleFromJson(JsonObject command);
CameraScheduleConfig getCameraSchedule();
