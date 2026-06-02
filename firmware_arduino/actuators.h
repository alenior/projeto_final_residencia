#pragma once

#include <Arduino.h>

void setupActuators();
void setPump(bool enabled);
void setLamp(bool enabled);
void setFan(bool enabled);
bool readMotion();
int readSoilRaw();
int readLdrRaw();
