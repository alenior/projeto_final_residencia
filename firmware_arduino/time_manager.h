#pragma once

#include <Arduino.h>
#include <time.h>

void setupTimeSync();
bool isTimeValid();
String nowIso8601();
String nowFileTimestamp();
bool getLocalTimeSafe(tm *out);
