#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

using SdPendingImageUploader = bool (*)(const char *path, const char *filename, const char *reason, const char *capturedAt, size_t sizeBytes);

void setupSdManager();
bool isSdReady();
bool sdSaveCameraImage(const String &filename, const uint8_t *data, size_t length, const String &capturedAt, const char *reason, bool uploaded);
bool sdAppendPendingImage(const String &filename, const String &capturedAt, const char *reason, size_t sizeBytes);
bool sdAppendLogJson(const char *module, const String &payloadJson);
bool sdQueueFirebaseJson(const char *module, const char *url, const char *token, const String &payloadJson);
void processPendingSdJsonUploads(uint8_t maxUploads);
bool sdAppendLogDocument(const char *module, JsonDocument &doc);
void processPendingSdImages(SdPendingImageUploader uploader, uint8_t maxUploads);
String sdImagePath(const String &filename);
