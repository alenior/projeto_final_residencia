#include "actuators.h"
#include "config.h"

namespace
{
    bool pumpState = false;
    bool lampState = false;
    bool fanState = false;

    void configureOutput(int pin)
    {
        if (pin < 0)
            return;
        pinMode(pin, OUTPUT);
    }

    void configureInput(int pin)
    {
        if (pin < 0)
            return;
        pinMode(pin, INPUT);
    }

    void writeRelay(int pin, bool enabled)
    {
        if (pin < 0)
            return;
        const bool level = RELAY_ACTIVE_LOW ? !enabled : enabled;
        digitalWrite(pin, level ? HIGH : LOW);
    }
}

void setupActuators()
{
    configureOutput(PIN_RELE_BOMBA);
    configureOutput(PIN_RELE_LAMPADA);
    configureOutput(PIN_VENTOINHA);
    configureInput(PIN_PIR);
    configureInput(PIN_SOLO_ADC);
    configureInput(PIN_LDR_ADC);

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
    Serial.printf("[ATUADOR] lampada_led=%s gpio=%d\n", enabled ? "ON" : "OFF", PIN_RELE_LAMPADA);
}

void setFan(bool enabled)
{
    fanState = enabled;
    writeRelay(PIN_VENTOINHA, enabled);
    if (PIN_VENTOINHA >= 0)
    {
        Serial.printf("[ATUADOR] ventoinha=%s\n", enabled ? "ON" : "OFF");
    }
}

bool isPumpOn()
{
    return pumpState;
}

bool isLampOn()
{
    return lampState;
}

bool isFanOn()
{
    return fanState;
}

bool readMotion()
{
    if (PIN_PIR < 0)
        return false;
    return digitalRead(PIN_PIR) == HIGH;
}

int readSoilRaw()
{
    if (PIN_SOLO_ADC < 0)
        return 0;
    return analogRead(PIN_SOLO_ADC);
}

int readLdrRaw()
{
    if (PIN_LDR_ADC < 0)
        return 0;
    return analogRead(PIN_LDR_ADC);
}
