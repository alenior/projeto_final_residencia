#pragma once

#include <Arduino.h>

void setupWiFi();
void ensureWiFiConnected();
bool isWiFiConnected();
String localIpString();

// Serializacao de uploads TLS: duas sessoes WiFiClientSecure em sequencia imediata
// corrompem o heap (crash no alocador TLSF / Interrupt WDT). Cada modulo deve checar
// tlsUploadSpacingElapsed() antes de conectar e chamar noteTlsUploadFinished() ao terminar.
bool tlsUploadSpacingElapsed(unsigned long minSpacingMs);
void noteTlsUploadFinished();
