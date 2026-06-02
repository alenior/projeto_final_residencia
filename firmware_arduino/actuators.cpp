#include "actuators.h"
#include "config.h"

namespace
{
    void writeRelay(uint8_t pin, bool enabled)
    {
        const bool level = RELAY_ACTIVE_LOW ? !enabled : enabled;
        digitalWrite(pin, level ? HIGH : LOW);
    }
}

void setupActuators()
{
    pinMode(PIN_RELE_BOMBA, OUTPUT);
    pinMode(PIN_RELE_LAMPADA, OUTPUT);
    pinMode(PIN_VENTOINHA, OUTPUT);
    pinMode(PIN_PIR, INPUT);
    pinMode(PIN_SOLO_ADC, INPUT);
    pinMode(PIN_LDR_ADC, INPUT);

    setPump(false);
    setLamp(false);
    setFan(false);
}

void setPump(bool enabled)
{
    writeRelay(PIN_RELE_BOMBA, enabled);
    Serial.printf("[ATUADOR] bomba=%s\n", enabled ? "ON" : "OFF");
}

void setLamp(bool enabled)
{
    writeRelay(PIN_RELE_LAMPADA, enabled);
    Serial.printf("[ATUADOR] lampada=%s\n", enabled ? "ON" : "OFF");
}

void setFan(bool enabled)
{
    writeRelay(PIN_VENTOINHA, enabled);
    Serial.printf("[ATUADOR] ventoinha=%s\n", enabled ? "ON" : "OFF");
}

bool readMotion()
{
    return digitalRead(PIN_PIR) == HIGH;
}

int readSoilRaw()
{
    return analogRead(PIN_SOLO_ADC);
}

int readLdrRaw()
{
    return analogRead(PIN_LDR_ADC);
}
