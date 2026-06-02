#pragma once

#include <Arduino.h>

void setupWiFi();
void ensureWiFiConnected();
bool isWiFiConnected();
String localIpString();
