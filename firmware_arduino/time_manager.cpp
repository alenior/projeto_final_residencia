#include "time_manager.h"
#include "config.h"

void setupTimeSync()
{
    configTime(GMT_OFFSET_SECONDS, DAYLIGHT_OFFSET_SECONDS, NTP_SERVER);
    Serial.printf("[NTP] Sincronizando com %s", NTP_SERVER);
    for (int i = 0; i < 20; i++)
    {
        if (isTimeValid())
        {
            Serial.printf("\n[NTP] Hora local atual: %s\n", nowIso8601().c_str());
            return;
        }
        delay(500);
        Serial.print('.');
    }
    Serial.println("\n[NTP][WARN] Hora ainda nao sincronizada.");
}

bool getLocalTimeSafe(tm *out)
{
    if (out == nullptr)
        return false;
    return getLocalTime(out, 100);
}

bool isTimeValid()
{
    time_t now = time(nullptr);
    return now > 1700000000;
}

String nowIso8601()
{
    tm timeinfo;
    if (!getLocalTimeSafe(&timeinfo))
        return "";
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buffer);
}

String nowFileTimestamp()
{
    tm timeinfo;
    if (!getLocalTimeSafe(&timeinfo))
    {
        return String("boot_") + String(millis());
    }
    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &timeinfo);
    return String(buffer);
}
