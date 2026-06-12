#include "wifi_manager.h"
#include "config.h"

#include <WiFi.h>

namespace
{
    unsigned long lastReconnectAttemptMs = 0;
    unsigned long lastTlsUploadEndMs = 0;
    bool tlsUploadEverDone = false;
}

bool tlsUploadSpacingElapsed(unsigned long minSpacingMs)
{
    if (!tlsUploadEverDone)
        return true;
    return static_cast<long>(millis() - lastTlsUploadEndMs) >= static_cast<long>(minSpacingMs);
}

void noteTlsUploadFinished()
{
    lastTlsUploadEndMs = millis();
    tlsUploadEverDone = true;
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("[WIFI] Conectando ao SSID '%s'", WIFI_SSID);
    const unsigned long startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000UL)
    {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[WIFI] conectado ip=%s rssi=%d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    else
    {
        Serial.println("[WIFI][WARN] Nao conectou no bootstrap; novas tentativas ocorrerao no loop.");
    }
}

void ensureWiFiConnected()
{
    if (WiFi.status() == WL_CONNECTED)
        return;
    if (millis() - lastReconnectAttemptMs < 5000UL)
        return;

    lastReconnectAttemptMs = millis();
    Serial.println("[WIFI][RECONN] Tentando reconectar...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool isWiFiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

String localIpString()
{
    if (!isWiFiConnected())
        return "0.0.0.0";
    return WiFi.localIP().toString();
}
